//! Standalone native HTTP3 transport gate. This binary exposes no browser API.
use serde_json::{json, Value};
use std::error::Error;
use std::fs::{self, File};
use std::io::Read;
use std::path::Path;
use std::process::ExitCode;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};
use tokio::time::{sleep, timeout};
use wtransport::endpoint::ConnectOptions;
use wtransport::tls::Sha256Digest;
use wtransport::{ClientConfig, Endpoint, VarInt};

const URL: &str = "https://127.0.0.1:4433/echo";
const ORIGIN: &str = "http://127.0.0.1:8765";
const CLOSE_CODE: VarInt = VarInt::from_u32(0);
const CASE_DEADLINE: Duration = Duration::from_secs(12);
const CLOSE_DEADLINE: Duration = Duration::from_secs(3);

#[derive(Debug)]
struct Failure {
    stage: &'static str,
    category: &'static str,
}
impl Failure {
    fn at(stage: &'static str, category: &'static str) -> Self { Self { stage, category } }
}

fn parse_pin(value: &Value, now_ms: u64) -> Result<[u8; 32], Failure> {
    let expires = value.get("expiresAt").and_then(Value::as_u64)
        .ok_or_else(|| Failure::at("certificate-manifest", "missing-expiry"))?;
    if expires <= now_ms { return Err(Failure::at("certificate-manifest", "expired")); }
    let bytes = value.get("certificateHash").and_then(Value::as_array)
        .ok_or_else(|| Failure::at("certificate-manifest", "missing-hash"))?;
    if bytes.len() != 32 { return Err(Failure::at("certificate-manifest", "invalid-hash-length")); }
    let mut digest = [0u8; 32];
    for (index, byte) in bytes.iter().enumerate() {
        digest[index] = byte.as_u64().filter(|n| *n <= 255)
            .ok_or_else(|| Failure::at("certificate-manifest", "invalid-hash-byte"))? as u8;
    }
    Ok(digest)
}

fn configuration(pin: Option<[u8; 32]>) -> Result<ClientConfig, Failure> {
    let builder = ClientConfig::builder().with_bind_default();
    let verifier = match pin {
        Some(bytes) => builder.with_server_certificate_hashes([Sha256Digest::new(bytes)]),
        None => builder.with_native_certs(),
    };
    Ok(verifier.max_idle_timeout(Some(Duration::from_secs(8)))
        .map_err(|_| Failure::at("configuration", "invalid-idle-timeout"))?
        .keep_alive_interval(None).build())
}

fn nonce(kind: &str) -> Result<Vec<u8>, Failure> {
    let mut entropy = [0u8; 32];
    File::open("/dev/urandom").and_then(|mut file| file.read_exact(&mut entropy))
        .map_err(|_| Failure::at("nonce", "os-random-unavailable"))?;
    let mut bytes = format!("lume-wt-{kind}:").into_bytes();
    bytes.extend_from_slice(&entropy);
    Ok(bytes)
}

// Keep the full error chain in memory only. A negative TLS control must produce
// a certificate-specific failure; a generic timeout/network outage is not a pass.
fn certificate_error(error: &(dyn Error + 'static)) -> bool {
    let mut item = Some(error);
    while let Some(current) = item {
        if current.to_string().to_ascii_lowercase().contains("certificate") { return true; }
        item = current.source();
    }
    false
}

fn failed(name: &str, error: Failure, cleanup: bool) -> Value {
    json!({"name":name,"state":"fail","stage":error.stage,"errorCategory":error.category,
        "endpointClosed":cleanup,"rawErrorOrPayloadRecorded":false})
}

async fn positive(pin: [u8; 32], name: &str, idle_seconds: u64, task_start: Instant) -> Value {
    let config = match configuration(Some(pin)) { Ok(config) => config, Err(error) => return failed(name,error,true) };
    let endpoint = match Endpoint::client(config) {
        Ok(endpoint) => endpoint,
        Err(_) => return failed(name,Failure::at("endpoint","socket-create-failed"),true),
    };
    let started = Instant::now();
    let operation = async {
        let options = ConnectOptions::builder(URL).add_header("origin", ORIGIN).build();
        let connection = endpoint.connect(options).await
            .map_err(|_| Failure::at("connect","http3-session-failed"))?;
        let negotiated_h3 = connection.quic_connection().handshake_data()
            .and_then(|data| data.downcast::<wtransport::quinn::crypto::rustls::HandshakeData>().ok())
            .map(|data| data.protocol.as_deref() == Some(b"h3".as_slice())).unwrap_or(false);
        if !negotiated_h3 { return Err(Failure::at("handshake","alpn-not-h3")); }
        let datagram_nonce = nonce("datagram")?;
        let stream_nonce = nonce("stream")?;
        if datagram_nonce == stream_nonce { return Err(Failure::at("nonce","nonces-not-distinct")); }
        connection.send_datagram(&datagram_nonce)
            .map_err(|_| Failure::at("datagram-send","send-failed"))?;
        let response = connection.receive_datagram().await
            .map_err(|_| Failure::at("datagram-receive","receive-failed"))?;
        if response.as_ref() != datagram_nonce.as_slice() { return Err(Failure::at("datagram-echo","payload-mismatch")); }
        let opening = connection.open_bi().await
            .map_err(|_| Failure::at("stream-open","open-failed"))?;
        let (mut send, mut receive) = opening.await
            .map_err(|_| Failure::at("stream-open","initialize-failed"))?;
        send.write_all(&stream_nonce).await
            .map_err(|_| Failure::at("stream-send","write-failed"))?;
        send.finish().await
            .map_err(|_| Failure::at("stream-send","finish-failed"))?;
        let mut echoed = vec![0u8; stream_nonce.len()];
        receive.read_exact(&mut echoed).await
            .map_err(|_| Failure::at("stream-receive","read-failed"))?;
        if echoed != stream_nonce { return Err(Failure::at("stream-echo","payload-mismatch")); }
        let mut extra = [0u8; 1];
        if receive.read(&mut extra).await
            .map_err(|_| Failure::at("stream-receive","end-of-stream-failed"))?.is_some() {
            return Err(Failure::at("stream-echo","trailing-payload"));
        }
        let idle_started = task_start.elapsed().as_secs_f64();
        if idle_seconds > 0 {
            println!("{}",json!({"phase":"connected-idle-start","elapsedSeconds":idle_started,"seconds":idle_seconds}));
            sleep(Duration::from_secs(idle_seconds)).await;
        }
        let idle_finished = task_start.elapsed().as_secs_f64();
        let stats = connection.quic_connection().stats();
        if stats.udp_tx.bytes == 0 || stats.udp_rx.bytes == 0 { return Err(Failure::at("transport-statistics","no-wire-bytes")); }
        connection.close(CLOSE_CODE,b"standalone probe complete");
        Ok(json!({"name":name,"state":"pass","alpn":"h3","transport":"QUIC over UDP",
            "datagramEchoMatched":true,"streamEchoMatched":true,"streamFinished":true,"freshNonces":true,
            "datagramPayloadBytes":datagram_nonce.len(),"streamPayloadBytes":stream_nonce.len(),
            "udpBytesSent":stats.udp_tx.bytes,"udpBytesReceived":stats.udp_rx.bytes,
            "idleWindowStartSeconds":idle_started,"idleWindowEndSeconds":idle_finished,
            "keepAliveEnabled":false,"certificateMode":"constrained-sha256-hash",
            "rawErrorOrPayloadRecorded":false}))
    };
    let outcome = match timeout(CASE_DEADLINE,operation).await {
        Ok(outcome) => outcome,
        Err(_) => Err(Failure::at("positive-case","deadline-exceeded")),
    };
    endpoint.close(CLOSE_CODE,b"standalone endpoint shutdown");
    let closed = timeout(CLOSE_DEADLINE,endpoint.wait_idle()).await.is_ok();
    let connections = endpoint.open_connections();
    drop(endpoint);
    let cleanup = closed && connections == 0;
    match outcome {
        Ok(mut report) if cleanup => {
            report["endpointClosed"] = json!(true);
            report["openConnectionsAfterClose"] = json!(connections);
            report["elapsedSeconds"] = json!(started.elapsed().as_secs_f64());
            report
        }
        Ok(_) => failed(name,Failure::at("teardown","endpoint-not-idle"),false),
        Err(error) => failed(name,error,cleanup),
    }
}

async fn negative(pin: Option<[u8; 32]>, name: &str) -> Value {
    let config = match configuration(pin) { Ok(config) => config, Err(error) => return failed(name,error,true) };
    let endpoint = match Endpoint::client(config) {
        Ok(endpoint) => endpoint,
        Err(_) => return failed(name,Failure::at("endpoint","socket-create-failed"),true),
    };
    let result = timeout(Duration::from_secs(5),endpoint.connect(ConnectOptions::builder(URL).add_header("origin",ORIGIN).build())).await;
    let (rejected, cert_specific, category) = match result {
        Ok(Err(error)) => (true,certificate_error(&error),"connection-rejected"),
        Ok(Ok(connection)) => { connection.close(CLOSE_CODE,b"unexpected acceptance"); (false,false,"unexpected-acceptance") },
        Err(_) => (false,false,"deadline-exceeded"),
    };
    endpoint.close(CLOSE_CODE,b"negative control complete");
    let closed = timeout(CLOSE_DEADLINE,endpoint.wait_idle()).await.is_ok();
    let connections = endpoint.open_connections();
    drop(endpoint);
    let passed = rejected && cert_specific && closed && connections == 0;
    json!({"name":name,"state":if passed {"pass"} else {"fail"},
        "connectionRejected":rejected,"certificateSpecificFailureObserved":cert_specific,
        "errorCategory":category,"endpointClosed":closed && connections==0,
        "openConnectionsAfterClose":connections,
        "certificateMode":if pin.is_some(){"constrained-sha256-hash"}else{"normal-native-roots"},
        "rawErrorOrPayloadRecorded":false})
}

async fn run(manifest: &Path, started: Instant) -> Result<Value, Failure> {
    let metadata = fs::metadata(manifest).map_err(|_| Failure::at("certificate-manifest","unreadable"))?;
    if metadata.len() > 8192 { return Err(Failure::at("certificate-manifest","oversized")); }
    let content = fs::read(manifest).map_err(|_| Failure::at("certificate-manifest","unreadable"))?;
    let value: Value = serde_json::from_slice(&content).map_err(|_| Failure::at("certificate-manifest","invalid-json"))?;
    let now = SystemTime::now().duration_since(UNIX_EPOCH)
        .map_err(|_| Failure::at("clock","invalid-system-time"))?.as_millis() as u64;
    let pin = parse_pin(&value,now)?;
    let first = positive(pin,"pinned-http3-before-controls",3,started).await;
    let mut cases = vec![first];
    if cases[0]["state"] == "pass" {
        let mut wrong = pin; wrong[0] ^= 0xff;
        cases.push(negative(Some(wrong),"wrong-pin-rejected").await);
        cases.push(negative(None,"native-roots-reject-self-signed").await);
        cases.push(positive(pin,"pinned-http3-after-controls",0,started).await);
    }
    let passed = cases.len()==4 && cases.iter().all(|case| case["state"]=="pass" && case["endpointClosed"]==true);
    Ok(json!({"schemaVersion":1,"state":if passed{"pass"}else{"fail"},
        "implementation":"wtransport-0.7.2","experiment":"standalone-native-http3-client",
        "lumenBrowserSupport":false,"browserConstructorExposed":false,"iosTargetsTested":false,
        "nativeRootsPositivePublicEndpointTested":false,
        "target":"loopback HTTP3-only aioquic server","tokioWorkerThreads":2,
        "certificateValidationDisabled":false,"noncePayloadsRecorded":false,"cases":cases,
        "elapsedSeconds":started.elapsed().as_secs_f64()}))
}

#[tokio::main(flavor="multi_thread",worker_threads=2)]
async fn main() -> ExitCode {
    let args: Vec<String> = std::env::args().collect();
    let started = Instant::now();
    let report = if args.len()==3 && args[1]=="--certificate-manifest" {
        match run(Path::new(&args[2]),started).await {
            Ok(report) => report,
            Err(error) => failed("initialization",error,true),
        }
    } else { failed("initialization",Failure::at("arguments","certificate-manifest-required"),true) };
    let output = Path::new("output/GPT6-AstraPro-subagents-WebTransport-gate.json");
    let saved = fs::create_dir_all("output").is_ok()
        && serde_json::to_vec_pretty(&report).ok().is_some_and(|bytes| fs::write(output,bytes).is_ok());
    println!("{}",report);
    if saved && report["state"]=="pass" {ExitCode::SUCCESS} else {ExitCode::FAILURE}
}

#[cfg(test)]
mod tests {
    use super::*;
    fn fixture() -> Value { json!({"expiresAt":2000,"certificateHash":vec![7u8;32]}) }
    #[test]
    fn accepts_only_exact_byte_hashes_and_live_manifest() {
        assert_eq!(parse_pin(&fixture(),1000).unwrap(),[7u8;32]);
        assert!(parse_pin(&fixture(),2000).is_err());
        let mut bad=fixture();bad["certificateHash"]=json!([7]);assert!(parse_pin(&bad,1000).is_err());
        bad=fixture();bad["certificateHash"][0]=json!(256);assert!(parse_pin(&bad,1000).is_err());
        bad=fixture();bad["certificateHash"][0]=json!(1.5);assert!(parse_pin(&bad,1000).is_err());
    }
    #[test]
    fn nonce_contains_fresh_os_random_bytes() {
        let one=nonce("fixture").unwrap();let two=nonce("fixture").unwrap();
        assert_eq!(one.len(),48);assert_ne!(one,two);
    }
    #[test]
    fn generic_timeout_is_not_a_certificate_control_pass() {
        let timeout=std::io::Error::new(std::io::ErrorKind::TimedOut,"operation timed out");
        assert!(!certificate_error(&timeout));
    }
}
