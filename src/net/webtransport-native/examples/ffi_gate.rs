//! Real protocol/lifecycle acceptance through exactly the public C ABI.
use lumen_webtransport_native::*;
use serde_json::{json, Value};
use std::collections::VecDeque;
use std::fs::{self, File};
use std::io::Read;
use std::ptr;
use std::time::{Duration, Instant};

type Check<T> = Result<T, &'static str>;
struct Event { metadata: Value, bytes: Vec<u8> }
struct Client { owner: u64, session: u64, request: u64, inbox: VecDeque<Event> }
impl Client {
    fn connect(origin: &str, hash: Option<&[u8]>) -> Check<Self> {
        let owner = lume_wt_owner_create(); if owner == 0 { return Err("owner-create"); }
        let mut options = json!({"url":"https://127.0.0.1:4433/echo","origin":origin,"requireUnreliable":true});
        if let Some(hash) = hash { options["certificateHashes"] = json!([hash]); }
        let input = options.to_string(); let mut session = 0;
        let status = unsafe { lume_wt_connect(owner,input.as_ptr(),input.len(),&mut session) };
        if status != OK { lume_wt_owner_drop(owner); return Err("connect-allocation"); }
        Ok(Self { owner, session, request: 0, inbox: VecDeque::new() })
    }
    fn wait(&mut self, accept: impl Fn(&Value) -> bool) -> Check<Event> {
        let started = Instant::now();
        loop {
            if let Some(index) = self.inbox.iter().position(|event| accept(&event.metadata)) { return Ok(self.inbox.remove(index).unwrap()); }
            let pointer = lume_wt_poll(self.owner);
            if !pointer.is_null() {
                let event = unsafe {
                    let mut length = 0;
                    let data = lume_wt_event_json(pointer,&mut length);
                    let metadata = serde_json::from_slice(std::slice::from_raw_parts(data,length));
                    let data = lume_wt_event_bytes(pointer,&mut length);
                    let bytes = std::slice::from_raw_parts(data,length).to_vec();
                    lume_wt_event_free(pointer);
                    Event { metadata: metadata.map_err(|_| "invalid-event-json")?, bytes }
                };
                if accept(&event.metadata) { return Ok(event); }
                if event.metadata["type"] == "closed" { return Err("unexpected-terminal-event"); }
                if self.inbox.len() >= 256 { return Err("test-inbox-limit"); }
                self.inbox.push_back(event);
            } else {
                if started.elapsed() > Duration::from_secs(15) { return Err("event-deadline"); }
                std::thread::sleep(Duration::from_millis(5));
            }
        }
    }
    fn ready(&mut self) -> Check<()> {
        let event = self.wait(|event| event["type"] == "ready")?;
        if event.metadata["alpn"] != "h3" || event.metadata["maxDatagramSize"].as_u64().unwrap_or(0) == 0 { return Err("invalid-real-ready"); }
        Ok(())
    }
    fn submit(&mut self, stream: u64, operation: u32, code: u32, bytes: &[u8]) -> Check<u64> {
        self.request += 1;
        let status = unsafe { lume_wt_command(self.owner,self.session,stream,operation,self.request,code,bytes.as_ptr(),bytes.len()) };
        if status != OK { return Err("command-rejected"); } Ok(self.request)
    }
    fn complete(&mut self, request: u64) -> Check<Event> {
        let event = self.wait(|event| event["requestId"] == request)?;
        if !["result","stats"].contains(&event.metadata["type"].as_str().unwrap_or("")) { return Err("command-failed"); }
        Ok(event)
    }
    fn command(&mut self, stream: u64, operation: u32, bytes: &[u8]) -> Check<Event> {
        let request = self.submit(stream,operation,0,bytes)?; self.complete(request)
    }
    fn datagram(&mut self, payload: &[u8]) -> Check<()> {
        self.command(0,DATAGRAM_WRITE,payload)?;
        let echo = self.command(0,DATAGRAM_READ,&[])?;
        if echo.bytes != payload { return Err("datagram-mismatch"); } Ok(())
    }
    fn stream(&mut self, payload: &[u8]) -> Check<()> {
        let event = self.command(0,CREATE_BI,&[])?;
        let stream = event.metadata["streamId"].as_u64().ok_or("missing-stream")?;
        let overlarge = vec![0u8;65537]; self.request += 1;
        if unsafe { lume_wt_command(self.owner,self.session,stream,STREAM_WRITE,self.request,0,overlarge.as_ptr(),overlarge.len()) } != INVALID { return Err("chunk-bound-not-enforced"); }
        self.command(stream,STREAM_WRITE,payload)?;
        self.command(stream,STREAM_FINISH,&[])?;
        let mut echo = Vec::new(); let mut chunks = 0;
        loop {
            let event = self.command(stream,STREAM_READ,&[])?;
            if event.metadata["done"] == true { break; }
            if event.bytes.is_empty() || echo.len()+event.bytes.len() > payload.len() { return Err("stream-trailing-or-empty"); }
            echo.extend(event.bytes); chunks += 1;
            if chunks > 64 { return Err("stream-chunk-limit"); }
        }
        if echo != payload || chunks < 2 { return Err("stream-exact-fragmented-echo"); } Ok(())
    }
    fn cancel_pending_read(&mut self) -> Check<()> {
        let event = self.command(0,CREATE_BI,&[])?;
        let stream = event.metadata["streamId"].as_u64().ok_or("missing-stream")?;
        let pending = self.submit(stream,STREAM_READ,0,&[])?;
        let cancelled = self.submit(stream,STREAM_CANCEL_READ,17,&[])?;
        let rejected = self.wait(|event| event["requestId"] == pending)?;
        if rejected.metadata["type"] != "error" || rejected.metadata["name"] != "AbortError" { return Err("pending-read-not-cancelled"); }
        self.complete(cancelled)?;
        let abort = self.submit(stream,STREAM_ABORT_WRITE,19,&[])?; self.complete(abort)?; Ok(())
    }
    fn cancel_receive_routes(&mut self) -> Check<()> {
        for (read,cancel) in [(DATAGRAM_READ,CANCEL_DATAGRAM_READ),(ACCEPT_BI,CANCEL_ACCEPT_BI),(ACCEPT_UNI,CANCEL_ACCEPT_UNI)] {
            let pending=self.submit(0,read,0,&[])?;
            let cancelling=self.submit(0,cancel,0,&[])?;
            let rejected=self.wait(|event|event["requestId"]==pending)?;
            if rejected.metadata["type"]!="error" || rejected.metadata["name"]!="AbortError" {return Err("receive-route-not-cancelled");}
            self.complete(cancelling)?;
            self.request+=1;
            if unsafe {lume_wt_command(self.owner,self.session,0,read,self.request,0,ptr::null(),0)}!=STATE {return Err("cancelled-route-reopened");}
        }
        Ok(())
    }
    fn close(&mut self) -> Check<()> {
        let reason = b"FFI protocol gate complete";
        if unsafe { lume_wt_command(self.owner,self.session,0,CLOSE,0,0,reason.as_ptr(),reason.len()) } != OK { return Err("close-command"); }
        let event = self.wait(|event| event["type"] == "closed")?;
        if event.metadata["clean"] != true || event.metadata["localCloseRequested"] != true { return Err("unclean-close"); }
        self.drop_owner()
    }
    fn drop_owner(&mut self) -> Check<()> {
        if self.owner != 0 {
            let old = self.owner; self.owner = 0;
            if lume_wt_owner_drop(old) != OK { return Err("owner-close-deadline"); }
            if !lume_wt_poll(old).is_null() { return Err("late-owner-event"); }
        }
        Ok(())
    }
}
impl Drop for Client { fn drop(&mut self) { if self.owner != 0 { let _ = lume_wt_owner_drop(self.owner); } } }
fn nonce(size: usize) -> Check<Vec<u8>> {
    let mut bytes = vec![0u8;size]; File::open("/dev/urandom").and_then(|mut file| file.read_exact(&mut bytes)).map_err(|_| "os-nonce")?; Ok(bytes)
}
fn empty_counts() -> Check<bool> {
    let mut counts = LumeWtCounts::default();
    if unsafe { lume_wt_counts(0,&mut counts) } != OK { return Err("resource-counts"); }
    Ok(counts.owners==0&&counts.sessions==0&&counts.streams==0&&counts.tasks==0&&counts.queued_event_bytes==0&&counts.pending_send_bytes==0)
}
fn run(manifest: &str, origin: &str) -> Check<Value> {
    if lume_wt_initialize(None) != OK { return Err("runtime-initialize"); }
    let manifest: Value = serde_json::from_slice(&fs::read(manifest).map_err(|_| "manifest-read")?).map_err(|_| "manifest-json")?;
    let values = manifest["certificateHash"].as_array().ok_or("manifest-pin")?;
    let pin: Vec<u8> = values.iter().map(|value| value.as_u64().filter(|byte|*byte<=255).map(|byte|byte as u8).ok_or("manifest-pin-byte")).collect::<Check<_>>()?;
    if pin.len()!=32 { return Err("manifest-pin-length"); }
    let started = Instant::now();
    let mut primary = Client::connect(origin,Some(&pin))?; primary.ready()?;
    let datagram = nonce(61)?; let stream = nonce(20000)?;
    primary.datagram(&datagram)?; primary.cancel_receive_routes()?; primary.stream(&stream)?; primary.cancel_pending_read()?;
    let stats = primary.command(0,STATS,&[])?.metadata;
    if stats["udpBytesSent"].as_u64().unwrap_or(0)==0 || stats["udpBytesReceived"].as_u64().unwrap_or(0)==0 || stats["streamPayloadBytesSent"]!=stream.len() || stats["streamPayloadBytesReceived"]!=stream.len() { return Err("measured-payload-counters"); }
    let mut second = Client::connect(origin,Some(&pin))?; second.ready()?;
    let ownership = unsafe { lume_wt_command(second.owner,primary.session,0,STATS,99999,0,ptr::null(),0) };
    if ownership != CLOSED { return Err("wrong-document-session-accepted"); }
    let event = primary.command(0,CREATE_BI,&[])?;
    let open_stream = event.metadata["streamId"].as_u64().ok_or("missing-stream")?;
    primary.submit(open_stream,STREAM_READ,0,&[])?;
    let teardown = Instant::now(); primary.drop_owner()?;
    let continuation = nonce(67)?; second.datagram(&continuation)?; second.close()?;
    if !empty_counts()? { return Err("positive-resources-not-released"); }
    let mut wrong_pin = pin.clone(); wrong_pin[0]^=255;
    for expected in [Some(wrong_pin.as_slice()),None] {
        let mut negative = Client::connect(origin,expected)?;
        let closed = negative.wait(|event| event["type"] == "closed")?;
        if closed.metadata["clean"]!=false || closed.metadata["errorName"]!="SecurityError" { return Err("negative-tls-not-certificate-specific"); }
        negative.drop_owner()?;
    }
    if !empty_counts()? { return Err("negative-resources-not-released"); }
    Ok(json!({"schemaVersion":1,"pass":true,"scope":"public C ABI real HTTP/3 protocol gate; browser API acceptance is separate",
        "datagramEchoMatched":true,"reliableStreamEchoMatched":true,"reliableStreamEOF":true,
        "fragmentedNativeReads":true,"oversizedWriteRejected":true,"pendingReadCancelled":true,
        "receiveRoutesCancelled":true,"cancelledRoutesRemainTerminal":true,"otherStreamWorksAfterRouteCancel":true,
        "independentDocumentContinuation":true,"wrongDocumentHandleRejected":true,
        "wrongPinCertificateRejected":true,"normalRootsSelfSignedCertificateRejected":true,
        "resourcesReleased":true,"datagramPayloadBytesSent":datagram.len()+continuation.len(),
        "streamPayloadBytesSent":stream.len(),"actualPrimaryStats":stats,
        "dropAndContinuationSeconds":teardown.elapsed().as_secs_f64(),"seconds":started.elapsed().as_secs_f64(),
        "rawPayloadOrCertificateRecorded":false,"browserConstructorExposed":false}))
}
fn main() {
    let args: Vec<_> = std::env::args().collect();
    let result = if args.len()==3 { run(&args[1],&args[2]) } else { Err("usage-manifest-and-origin-required") };
    match result {
        Ok(report) => println!("{}",report),
        Err(stage) => { println!("{}",json!({"schemaVersion":1,"pass":false,"stage":stage,"rawPayloadOrCertificateRecorded":false,"browserConstructorExposed":false})); std::process::exit(1); }
    }
}
