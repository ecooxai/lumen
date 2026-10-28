//! Bounded, document-owned HTTP/3 WebTransport client. No V8 pointers cross this ABI.
//! See CONTRACT.md for limits and the explicitly incomplete browser API profile.
use serde_json::{json, Value};
use std::collections::{HashMap, HashSet, VecDeque};
use std::future::{poll_fn, Future};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr;
use std::sync::atomic::{AtomicBool, AtomicU64, AtomicU8, AtomicUsize, Ordering};
use std::sync::{Arc, Condvar, Mutex, MutexGuard, OnceLock, Weak};
use std::time::{Duration, Instant};
use std::task::Poll;
use tokio::runtime::{Builder, Runtime};
use tokio::sync::{mpsc, watch, Mutex as AsyncMutex};
use tokio::task::{AbortHandle, JoinSet};
use tokio::time::timeout;
use wtransport::endpoint::ConnectOptions;
use wtransport::stream::{RecvStream, SendStream};
use wtransport::tls::Sha256Digest;
use wtransport::{ClientConfig, Connection, Endpoint, VarInt};

pub const OK: i32 = 0;
pub const INVALID: i32 = -1;
pub const CLOSED: i32 = -2;
pub const QUOTA: i32 = -3;
pub const STATE: i32 = -4;
pub const UNSUPPORTED: i32 = -5;
pub const TIMEOUT: i32 = -6;
pub const DATAGRAM_WRITE: u32 = 1;
pub const DATAGRAM_READ: u32 = 2;
pub const CREATE_BI: u32 = 3;
pub const CREATE_UNI: u32 = 4;
pub const ACCEPT_BI: u32 = 5;
pub const ACCEPT_UNI: u32 = 6;
pub const STREAM_READ: u32 = 7;
pub const STREAM_WRITE: u32 = 8;
pub const STREAM_FINISH: u32 = 9;
pub const STREAM_CANCEL_READ: u32 = 10;
pub const STREAM_ABORT_WRITE: u32 = 11;
pub const CLOSE: u32 = 12;
pub const STATS: u32 = 13;
pub const CANCEL_DATAGRAM_READ: u32 = 14;
pub const CANCEL_ACCEPT_BI: u32 = 15;
pub const CANCEL_ACCEPT_UNI: u32 = 16;
const MAX_SAFE_ID: u64 = (1u64 << 53) - 1;
const MAX_SESSIONS: usize = 4;
const MAX_STREAMS: usize = 32;
const MAX_REQUESTS: usize = 128;
const MAX_CHUNK: usize = 65536;
const READ_CHUNK: usize = 16384;
const MAX_SEND_BYTES: usize = 4 * 1024 * 1024;
const MAX_EVENT_BYTES: usize = 4 * 1024 * 1024;
const MAX_EVENTS: usize = 256;
const CONNECT_TIMEOUT: Duration = Duration::from_secs(12);
const ENDPOINT_CLOSE_TIMEOUT: Duration = Duration::from_millis(1000);
const OWNER_CLOSE_TIMEOUT: Duration = Duration::from_millis(2000);
const H3_NO_ERROR: VarInt = VarInt::from_u32(0x100);
type Id = u64;
type Wake = extern "C" fn();

fn lock<T>(mutex: &Mutex<T>) -> MutexGuard<'_, T> {
    mutex.lock().unwrap_or_else(|poisoned| poisoned.into_inner())
}
fn safe_id(counter: &AtomicU64) -> Option<Id> {
    counter.fetch_update(Ordering::SeqCst, Ordering::SeqCst,
        |next| if next > MAX_SAFE_ID { None } else { Some(next + 1) }).ok()
}
fn reserve(counter: &AtomicUsize, amount: usize, maximum: usize) -> bool {
    counter.fetch_update(Ordering::SeqCst, Ordering::SeqCst,
        |used| used.checked_add(amount).filter(|total| *total <= maximum)).is_ok()
}
// WebTransport application errors skip HTTP/3's reserved GREASE codepoints.
fn stream_wire_code(code: u32) -> VarInt {
    VarInt::try_from_u64(0x52e4a40fa8db + code as u64 + code as u64 / 0x1e)
        .expect("A u32 WebTransport application code fits a QUIC varint")
}
fn stream_application_code(code: u64) -> Option<u32> {
    let first = 0x52e4a40fa8dbu64;
    let last = 0x52e5ac983162u64;
    if !(first..=last).contains(&code) || (code - 0x21) % 0x1f == 0 { return None; }
    let offset = code - first;
    u32::try_from(offset - offset / 0x1f).ok()
}

struct Engine {
    runtime: Runtime,
    owners: Mutex<HashMap<Id, Arc<Owner>>>,
    next_id: AtomicU64,
    wake: Option<Wake>,
    owners_live: AtomicUsize,
    sessions_live: AtomicUsize,
    streams_live: AtomicUsize,
    tasks_live: AtomicUsize,
    event_bytes: AtomicUsize,
    send_bytes: AtomicUsize,
}
static ENGINE: OnceLock<Engine> = OnceLock::new();
fn engine() -> Result<&'static Engine, i32> { ENGINE.get().ok_or(STATE) }

#[repr(C)]
#[derive(Default, Clone, Copy, Debug)]
pub struct LumeWtCounts {
    pub owners: u64,
    pub sessions: u64,
    pub streams: u64,
    pub tasks: u64,
    pub queued_event_bytes: u64,
    pub pending_send_bytes: u64,
}
pub struct LumeWtEvent { json: Vec<u8>, bytes: Vec<u8>, terminal_session: Option<Id> }
impl LumeWtEvent {
    fn new(metadata: Value, bytes: Vec<u8>) -> Self {
        Self { json: metadata.to_string().into_bytes(), bytes, terminal_session: None }
    }
    fn size(&self) -> usize { self.json.len() + self.bytes.len() + std::mem::size_of::<Self>() }
}
#[derive(Default)]
struct EventQueue { items: VecDeque<LumeWtEvent>, bytes: usize }
struct Owner {
    id: Id,
    closing: AtomicBool,
    overflowing: AtomicBool,
    events: Mutex<EventQueue>,
    sessions: Mutex<HashMap<Id, Arc<Session>>>,
    // Keep the session quota until its terminal notification is polled. This
    // bounds terminal retention even if a C caller repeatedly opens and closes.
    handles: Mutex<HashSet<Id>>,
    tasks: AtomicUsize,
    pending_send: AtomicUsize,
    idle: Mutex<()>,
    task_changed: Condvar,
}
impl Owner {
    fn new(id: Id) -> Self {
        ENGINE.get().unwrap().owners_live.fetch_add(1, Ordering::SeqCst);
        Self { id, closing: AtomicBool::new(false), overflowing: AtomicBool::new(false),
            events: Mutex::new(EventQueue::default()), sessions: Mutex::new(HashMap::new()),
            handles: Mutex::new(HashSet::new()),
            tasks: AtomicUsize::new(0), pending_send: AtomicUsize::new(0),
            idle: Mutex::new(()), task_changed: Condvar::new() }
    }
    fn active(&self) -> Result<(), i32> {
        if self.closing.load(Ordering::SeqCst) { Err(CLOSED) }
        else if self.overflowing.load(Ordering::SeqCst) { Err(QUOTA) } else { Ok(()) }
    }
    fn clear_events(&self, queue: &mut EventQueue) {
        ENGINE.get().unwrap().event_bytes.fetch_sub(queue.bytes, Ordering::SeqCst);
        queue.items.clear(); queue.bytes = 0;
    }
    fn emit(&self, event: LumeWtEvent, terminal: bool) {
        let mut queue = lock(&self.events);
        if self.closing.load(Ordering::SeqCst) || (self.overflowing.load(Ordering::SeqCst) && !terminal) { return; }
        let size = event.size();
        if queue.items.len() >= MAX_EVENTS || size > MAX_EVENT_BYTES.saturating_sub(queue.bytes) {
            let old_bytes = queue.bytes;
            queue.items.retain(|queued| queued.terminal_session.is_some());
            queue.bytes = queue.items.iter().map(LumeWtEvent::size).sum();
            ENGINE.get().unwrap().event_bytes.fetch_sub(old_bytes - queue.bytes, Ordering::SeqCst);
            let first = !self.overflowing.swap(true, Ordering::SeqCst);
            // A terminal event must remain deliverable even when it caused the
            // overflow: this session has already left the live-session map.
            if terminal && size <= MAX_EVENT_BYTES {
                queue.bytes += size;
                ENGINE.get().unwrap().event_bytes.fetch_add(size, Ordering::SeqCst);
                queue.items.push_back(event);
            }
            drop(queue);
            if first {
                let sessions: Vec<_> = lock(&self.sessions).values().cloned().collect();
                for session in sessions { session.shutdown(CloseInfo::failure("QuotaExceededError", "Native event queue limit exceeded")); }
            }
            if let Some(wake) = ENGINE.get().and_then(|state| state.wake) { wake(); }
            return;
        }
        queue.bytes += size;
        ENGINE.get().unwrap().event_bytes.fetch_add(size, Ordering::SeqCst);
        queue.items.push_back(event); drop(queue);
        if !self.closing.load(Ordering::SeqCst) {
            if let Some(wake) = ENGINE.get().and_then(|state| state.wake) { wake(); }
        }
    }
    fn shutdown(&self) {
        // This lock is also held by emit: after closing is set, no event can be enqueued.
        let mut queue = lock(&self.events);
        self.closing.store(true, Ordering::SeqCst);
        self.clear_events(&mut queue); drop(queue);
        let sessions: Vec<_> = lock(&self.sessions).values().cloned().collect();
        for session in sessions { session.shutdown(CloseInfo::failure("AbortError", "Document transport owner was destroyed")); }
    }
}
struct TaskGuard(Arc<Owner>);
impl TaskGuard {
    fn new(owner: Arc<Owner>) -> Self {
        owner.tasks.fetch_add(1, Ordering::SeqCst);
        ENGINE.get().unwrap().tasks_live.fetch_add(1, Ordering::SeqCst);
        Self(owner)
    }
}
impl Drop for TaskGuard {
    fn drop(&mut self) {
        let _guard = lock(&self.0.idle);
        self.0.tasks.fetch_sub(1, Ordering::SeqCst);
        ENGINE.get().unwrap().tasks_live.fetch_sub(1, Ordering::SeqCst);
        self.0.task_changed.notify_all();
    }
}

#[derive(Clone)]
struct CloseInfo { clean: bool, local: bool, code: u32, reason: String, error_name: &'static str, error_message: &'static str }
impl CloseInfo {
    fn failure(name: &'static str, message: &'static str) -> Self {
        Self { clean: false, local: false, code: 0, reason: String::new(), error_name: name, error_message: message }
    }
    fn event(&self, id: Id) -> LumeWtEvent {
        let mut value = json!({"type":"closed","sessionId":id,"clean":self.clean,
            "localCloseRequested":self.local,"closeCode":self.code,"reason":self.reason});
        if !self.clean { value["errorName"] = json!(self.error_name); value["errorMessage"] = json!(self.error_message); }
        let mut event = LumeWtEvent::new(value, Vec::new());
        event.terminal_session = Some(id); event
    }
}
#[derive(Default)]
struct PayloadStats { datagram_sent: AtomicU64, datagram_received: AtomicU64, stream_sent: AtomicU64, stream_received: AtomicU64 }
struct PullRoute {
    busy: Arc<AtomicBool>,
    cancellation: watch::Sender<bool>,
    completed: watch::Sender<u64>,
    publication: Mutex<()>,
}
impl PullRoute {
    fn new() -> Arc<Self> {
        let (cancellation, _) = watch::channel(false);
        let (completed, _) = watch::channel(0);
        Arc::new(Self { busy: Arc::new(AtomicBool::new(false)), cancellation, completed, publication: Mutex::new(()) })
    }
    fn cancelled(&self) -> bool { *self.cancellation.borrow() }
    async fn cancel(&self) {
        {
            // Linearize cancellation against publication of an accepted stream.
            let _guard = lock(&self.publication);
            self.cancellation.send_replace(true);
        }
        let mut completed = self.completed.subscribe();
        while self.busy.load(Ordering::SeqCst) {
            if completed.changed().await.is_err() { break; }
        }
    }
}
struct Session {
    id: Id,
    owner: Weak<Owner>,
    sender: mpsc::Sender<Command>,
    status: AtomicU8, // connecting=0, ready=1, closing=2, closed=3
    cancellation: watch::Sender<bool>,
    close_info: Mutex<Option<CloseInfo>>,
    abort: Mutex<Option<AbortHandle>>,
    requests: Mutex<HashSet<Id>>,
    streams: Mutex<HashMap<Id, Arc<Stream>>>,
    stream_slots: AtomicUsize,
    datagram_read: Arc<PullRoute>,
    accept_bi: Arc<PullRoute>,
    accept_uni: Arc<PullRoute>,
    stats: PayloadStats,
}
impl Session {
    fn shutdown(&self, info: CloseInfo) {
        let mut selected = lock(&self.close_info);
        if selected.is_none() { *selected = Some(info); }
        self.status.store(2, Ordering::SeqCst);
        self.cancellation.send_replace(true);
    }
    fn remove_completed_stream(&self, id: Id) {
        let mut streams = lock(&self.streams);
        if streams.get(&id).map(|stream| stream.send_done.load(Ordering::SeqCst) && stream.recv_done.load(Ordering::SeqCst)).unwrap_or(false) {
            streams.remove(&id);
        }
    }
}
impl Drop for Session {
    fn drop(&mut self) { ENGINE.get().unwrap().sessions_live.fetch_sub(1, Ordering::SeqCst); }
}
struct Stream {
    id: Id,
    session: Weak<Session>,
    send: AsyncMutex<Option<SendStream>>,
    recv: AsyncMutex<Option<RecvStream>>,
    send_busy: Arc<AtomicBool>,
    recv_busy: Arc<AtomicBool>,
    send_done: AtomicBool,
    recv_done: AtomicBool,
    send_cancel: watch::Sender<Option<u32>>,
    recv_cancel: watch::Sender<Option<u32>>,
}
impl Drop for Stream {
    fn drop(&mut self) {
        if let Some(session) = self.session.upgrade() { session.stream_slots.fetch_sub(1, Ordering::SeqCst); }
        ENGINE.get().unwrap().streams_live.fetch_sub(1, Ordering::SeqCst);
    }
}
struct Permit {
    owner: Arc<Owner>, session: Arc<Session>, request: Id,
    busy: Option<Arc<AtomicBool>>, route: Option<Arc<PullRoute>>, reserved_stream: bool, send_bytes: usize,
}
impl Drop for Permit {
    fn drop(&mut self) {
        lock(&self.session.requests).remove(&self.request);
        if self.reserved_stream { self.session.stream_slots.fetch_sub(1, Ordering::SeqCst); }
        if self.send_bytes != 0 {
            self.owner.pending_send.fetch_sub(self.send_bytes, Ordering::SeqCst);
            ENGINE.get().unwrap().send_bytes.fetch_sub(self.send_bytes, Ordering::SeqCst);
        }
        if let Some(busy) = &self.busy { busy.store(false, Ordering::SeqCst); }
        if let Some(route) = &self.route { route.completed.send_modify(|version| *version = version.wrapping_add(1)); }
    }
}
struct Command { operation: u32, code: u32, stream: Option<Arc<Stream>>, installed_stream: Option<Id>, payload: Vec<u8>, permit: Permit }
#[derive(Clone, Debug)]
struct Config { url: String, origin: String, hashes: Vec<[u8; 32]>, require_unreliable: bool }
impl Config {
    fn parse(bytes: &[u8]) -> Result<Self, i32> {
        if bytes.len() > 8192 { return Err(INVALID); }
        let value: Value = serde_json::from_slice(bytes).map_err(|_| INVALID)?;
        let object = value.as_object().ok_or(INVALID)?;
        if object.keys().any(|key| !["url", "origin", "certificateHashes", "requireUnreliable"].contains(&key.as_str())) { return Err(INVALID); }
        let text = object.get("url").and_then(Value::as_str).filter(|text| text.len() <= 4096).ok_or(INVALID)?;
        let parsed = url::Url::parse(text).map_err(|_| INVALID)?;
        if parsed.scheme() != "https" || parsed.host_str().is_none() || !parsed.username().is_empty() || parsed.password().is_some() || parsed.fragment().is_some() { return Err(INVALID); }
        let origin = object.get("origin").and_then(Value::as_str).filter(|text| text.len() <= 2048).ok_or(INVALID)?;
        if origin != "null" {
            let origin_url = url::Url::parse(origin).map_err(|_| INVALID)?;
            if !["http", "https"].contains(&origin_url.scheme()) || origin_url.origin().ascii_serialization() != origin || origin_url.path() != "/" || origin_url.query().is_some() || origin_url.fragment().is_some() || !origin_url.username().is_empty() || origin_url.password().is_some() { return Err(INVALID); }
        }
        let mut hashes = Vec::new();
        if let Some(entries) = object.get("certificateHashes") {
            let entries = entries.as_array().filter(|items| !items.is_empty() && items.len() <= 8).ok_or(INVALID)?;
            for entry in entries {
                let values = entry.as_array().filter(|bytes| bytes.len() == 32).ok_or(INVALID)?;
                let mut hash = [0u8; 32];
                for (index, value) in values.iter().enumerate() { hash[index] = value.as_u64().filter(|value| *value <= 255).ok_or(INVALID)? as u8; }
                hashes.push(hash);
            }
        }
        let require_unreliable = match object.get("requireUnreliable") { Some(value) => value.as_bool().ok_or(INVALID)?, None => false };
        Ok(Self { url: parsed.into(), origin: origin.into(), hashes, require_unreliable })
    }
    fn client(&self) -> Result<ClientConfig, i32> {
        let builder = ClientConfig::builder().with_bind_default();
        let verified = if self.hashes.is_empty() { builder.with_native_certs() }
            else { builder.with_server_certificate_hashes(self.hashes.iter().copied().map(Sha256Digest::new)) };
        let mut config = verified.max_idle_timeout(Some(Duration::from_secs(30))).map_err(|_| STATE)?
            .keep_alive_interval(None).build();
        let mut transport = wtransport::quinn::TransportConfig::default();
        transport.max_concurrent_bidi_streams(wtransport::quinn::VarInt::from_u32(32));
        // HTTP/3 control/QPACK streams also consume unidirectional stream credit.
        transport.max_concurrent_uni_streams(wtransport::quinn::VarInt::from_u32(36));
        transport.stream_receive_window(wtransport::quinn::VarInt::from_u32(65536));
        transport.receive_window(wtransport::quinn::VarInt::from_u32(1024 * 1024));
        transport.send_window(1024 * 1024);
        transport.datagram_receive_buffer_size(Some(65536));
        transport.datagram_send_buffer_size(65536);
        transport.max_idle_timeout(Some(Duration::from_secs(30).try_into().map_err(|_| STATE)?));
        transport.keep_alive_interval(None);
        config.quic_config_mut().transport_config(Arc::new(transport));
        Ok(config)
    }
}

#[derive(Debug)]
struct OpError { name: &'static str, message: &'static str, stream_code: Option<u32> }
impl OpError {
    fn network() -> Self { Self { name: "NetworkError", message: "Native transport operation failed", stream_code: None } }
    fn aborted() -> Self { Self { name: "AbortError", message: "Native stream operation was cancelled", stream_code: None } }
    fn closed() -> Self { Self { name: "InvalidStateError", message: "Native stream half is closed", stream_code: None } }
    fn quota() -> Self { Self { name: "QuotaExceededError", message: "Native transport resource limit exceeded", stream_code: None } }
    fn event(&self, session: Id, request: Id) -> LumeWtEvent {
        let mut value = json!({"type":"error","sessionId":session,"requestId":request,"name":self.name,"message":self.message});
        if let Some(code) = self.stream_code { value["streamErrorCode"] = json!(code); }
        LumeWtEvent::new(value, Vec::new())
    }
}
fn read_error(error: wtransport::error::StreamReadError) -> OpError {
    let mut result = OpError::network();
    if let wtransport::error::StreamReadError::Reset(code) = error { result.stream_code = stream_application_code(code.into_inner()); }
    result
}
fn write_error(error: wtransport::error::StreamWriteError) -> OpError {
    let mut result = OpError::network();
    if let wtransport::error::StreamWriteError::Stopped(code) = error { result.stream_code = stream_application_code(code.into_inner()); }
    result
}
fn result_event(command: &Command, extra: Value, bytes: Vec<u8>) -> LumeWtEvent {
    let mut value = json!({"type":"result","sessionId":command.permit.session.id,
        "requestId":command.permit.request,"operation":command.operation});
    if let Value::Object(fields) = extra { for (key, content) in fields { value[key] = content; } }
    LumeWtEvent::new(value, bytes)
}

fn prepare_command(owner: Arc<Owner>, session: Arc<Session>, stream_id: Id,
    operation: u32, request: Id, code: u32, payload: &[u8]) -> Result<Command, i32> {
    owner.active()?;
    if session.status.load(Ordering::SeqCst) != 1 { return Err(STATE); }
    if request == 0 || request > MAX_SAFE_ID || !(DATAGRAM_WRITE..=CANCEL_ACCEPT_UNI).contains(&operation) || operation == CLOSE { return Err(INVALID); }
    if [CREATE_BI, CREATE_UNI].contains(&operation) {
        if code > 1 { return Err(INVALID); }
    } else if ![STREAM_CANCEL_READ, STREAM_ABORT_WRITE].contains(&operation) && code != 0 { return Err(INVALID); }
    if payload.len() > MAX_CHUNK || (![DATAGRAM_WRITE, STREAM_WRITE].contains(&operation) && !payload.is_empty()) { return Err(INVALID); }
    let stream_operation = (STREAM_READ..=STREAM_ABORT_WRITE).contains(&operation);
    let stream = if stream_operation {
        let found = lock(&session.streams).get(&stream_id).cloned().ok_or(STATE)?;
        if [STREAM_READ, STREAM_CANCEL_READ].contains(&operation) && found.recv_done.load(Ordering::SeqCst) { return Err(STATE); }
        if [STREAM_WRITE, STREAM_FINISH, STREAM_ABORT_WRITE].contains(&operation) && found.send_done.load(Ordering::SeqCst) { return Err(STATE); }
        Some(found)
    } else {
        if stream_id != 0 { return Err(INVALID); }
        None
    };
    {
        let mut requests = lock(&session.requests);
        if requests.len() >= MAX_REQUESTS { return Err(QUOTA); }
        if !requests.insert(request) { return Err(INVALID); }
    }
    let mut permit = Permit { owner, session, request, busy: None, route: None, reserved_stream: false, send_bytes: 0 };
    let route = match operation {
        DATAGRAM_READ => Some(permit.session.datagram_read.clone()),
        ACCEPT_BI => Some(permit.session.accept_bi.clone()),
        ACCEPT_UNI => Some(permit.session.accept_uni.clone()),
        _ => None,
    };
    if let Some(route) = route {
        if route.cancelled() { return Err(STATE); }
        permit.route = Some(route);
    }
    let busy = match operation {
        DATAGRAM_READ | ACCEPT_BI | ACCEPT_UNI => permit.route.as_ref().map(|route| route.busy.clone()),
        STREAM_READ => stream.as_ref().map(|stream| stream.recv_busy.clone()),
        STREAM_WRITE | STREAM_FINISH => stream.as_ref().map(|stream| stream.send_busy.clone()),
        _ => None,
    };
    if let Some(busy) = busy {
        if busy.compare_exchange(false, true, Ordering::SeqCst, Ordering::SeqCst).is_err() { return Err(STATE); }
        permit.busy = Some(busy);
    }
    if [CREATE_BI, CREATE_UNI, ACCEPT_BI, ACCEPT_UNI].contains(&operation) {
        if !reserve(&permit.session.stream_slots, 1, MAX_STREAMS) { return Err(QUOTA); }
        permit.reserved_stream = true;
    }
    if !payload.is_empty() {
        if !reserve(&permit.owner.pending_send, payload.len(), MAX_SEND_BYTES) { return Err(QUOTA); }
        permit.send_bytes = payload.len();
        ENGINE.get().unwrap().send_bytes.fetch_add(payload.len(), Ordering::SeqCst);
    }
    Ok(Command { operation, code, stream, installed_stream: None, payload: payload.to_vec(), permit })
}
fn install_stream(command: &mut Command, send: Option<SendStream>, recv: Option<RecvStream>) -> Result<Id, OpError> {
    let id = safe_id(&ENGINE.get().unwrap().next_id).ok_or_else(OpError::quota)?;
    let (send_cancel, _) = watch::channel(None);
    let (recv_cancel, _) = watch::channel(None);
    let stream = Arc::new(Stream { id, session: Arc::downgrade(&command.permit.session),
        send_done: AtomicBool::new(send.is_none()), recv_done: AtomicBool::new(recv.is_none()),
        send: AsyncMutex::new(send), recv: AsyncMutex::new(recv),
        send_busy: Arc::new(AtomicBool::new(false)), recv_busy: Arc::new(AtomicBool::new(false)), send_cancel, recv_cancel });
    ENGINE.get().unwrap().streams_live.fetch_add(1, Ordering::SeqCst);
    command.permit.reserved_stream = false;
    lock(&command.permit.session.streams).insert(id, stream);
    command.installed_stream = Some(id);
    Ok(id)
}

async fn pull_until_cancelled<T>(route: Arc<PullRoute>, future: impl Future<Output = Result<T, OpError>>) -> Result<T, OpError> {
    let mut cancellation = route.cancellation.subscribe();
    if route.cancelled() { return Err(OpError::aborted()); }
    tokio::select! {
        biased;
        _ = cancellation.changed() => Err(OpError::aborted()),
        result = future => result,
    }
}
async fn acquire_credit<T, E>(future: impl Future<Output = Result<T, E>>, wait: bool) -> Result<T, OpError> {
    let mut future = std::pin::pin!(future);
    if wait { return future.await.map_err(|_| OpError::network()); }
    // Opening QUIC credit is documented cancel-safe. A Pending first poll
    // reserves no stream and is not a synthetic guess about peer capacity.
    poll_fn(|context| Poll::Ready(match future.as_mut().poll(context) {
        Poll::Ready(value) => Some(value), Poll::Pending => None,
    })).await.ok_or_else(OpError::quota)?.map_err(|_| OpError::network())
}

fn publish_pull(command: Command, task_guard: TaskGuard, event: LumeWtEvent) -> Option<(Command, TaskGuard)> {
    let route = command.permit.route.as_ref().expect("pull route").clone();
    let _publication = lock(&route.publication);
    if route.cancelled() { return Some((command, task_guard)); }
    let owner = command.permit.owner.clone();
    drop(command); drop(task_guard);
    owner.emit(event, false);
    None
}
async fn discard_unpublished_stream(command: &Command) {
    let Some(id) = command.installed_stream else { return; };
    let stream = lock(&command.permit.session.streams).remove(&id);
    if let Some(stream) = stream {
        stream.send_cancel.send_replace(Some(0)); stream.recv_cancel.send_replace(Some(0));
        if let Some(mut send) = stream.send.lock().await.take() { let _ = send.reset(stream_wire_code(0)); }
        if let Some(recv) = stream.recv.lock().await.take() { recv.stop(stream_wire_code(0)); }
        stream.send_done.store(true, Ordering::SeqCst); stream.recv_done.store(true, Ordering::SeqCst);
    }
}

async fn read_stream(command: &Command) -> Result<LumeWtEvent, OpError> {
    let stream = command.stream.as_ref().ok_or_else(OpError::closed)?;
    let mut cancellation = stream.recv_cancel.subscribe();
    let mut holder = stream.recv.lock().await;
    let mut buffer = vec![0u8; READ_CHUNK];
    let cancelled = *cancellation.borrow();
    let outcome = if cancelled.is_some() { Err(OpError::aborted()) } else {
        let receive = holder.as_mut().ok_or_else(OpError::closed)?;
        tokio::select! {
            biased;
            _ = cancellation.changed() => Err(OpError::aborted()),
            outcome = receive.read(&mut buffer) => outcome.map_err(read_error),
        }
    };
    match outcome {
        Ok(Some(length)) if length > 0 => {
            buffer.truncate(length);
            command.permit.session.stats.stream_received.fetch_add(length as u64, Ordering::SeqCst);
            Ok(result_event(command, json!({"streamId":stream.id,"done":false}), buffer))
        }
        Ok(None) => {
            holder.take(); stream.recv_done.store(true, Ordering::SeqCst);
            Ok(result_event(command, json!({"streamId":stream.id,"done":true}), Vec::new()))
        }
        other => {
            if let Some(receive) = holder.take() { receive.stop(stream_wire_code(cancellation.borrow().unwrap_or(0))); }
            stream.recv_done.store(true, Ordering::SeqCst);
            Err(match other { Err(error) => error, _ => OpError::network() })
        }
    }
}
async fn write_stream(command: &Command) -> Result<LumeWtEvent, OpError> {
    let stream = command.stream.as_ref().ok_or_else(OpError::closed)?;
    let mut cancellation = stream.send_cancel.subscribe();
    let mut holder = stream.send.lock().await;
    let cancelled = *cancellation.borrow();
    let outcome = if cancelled.is_some() { Err(OpError::aborted()) } else {
        let send = holder.as_mut().ok_or_else(OpError::closed)?;
        tokio::select! {
            biased;
            _ = cancellation.changed() => Err(OpError::aborted()),
            outcome = async {
                if command.operation == STREAM_FINISH { send.finish().await }
                else {
                    let mut offset = 0;
                    while offset < command.payload.len() {
                        let count = send.write(&command.payload[offset..]).await?;
                        if count == 0 { return Err(wtransport::error::StreamWriteError::Closed); }
                        offset += count;
                        command.permit.session.stats.stream_sent.fetch_add(count as u64, Ordering::SeqCst);
                    }
                    Ok(())
                }
            } => outcome.map_err(write_error),
        }
    };
    match outcome {
        Ok(()) => {
            if command.operation == STREAM_FINISH { holder.take(); stream.send_done.store(true, Ordering::SeqCst); }
            Ok(result_event(command, json!({"streamId":stream.id}), Vec::new()))
        }
        Err(error) => {
            if let Some(mut send) = holder.take() { let _ = send.reset(stream_wire_code(cancellation.borrow().unwrap_or(0))); }
            stream.send_done.store(true, Ordering::SeqCst); Err(error)
        }
    }
}
async fn execute(command: &mut Command, connection: &Connection) -> Result<LumeWtEvent, OpError> {
    match command.operation {
        DATAGRAM_WRITE => {
            let maximum = connection.max_datagram_size().ok_or_else(OpError::network)?;
            if command.payload.len() > maximum { return Err(OpError { name: "RangeError", message: "Datagram exceeds the negotiated maximum", stream_code: None }); }
            connection.send_datagram(&command.payload).map_err(|_| OpError::network())?;
            command.permit.session.stats.datagram_sent.fetch_add(command.payload.len() as u64, Ordering::SeqCst);
            Ok(result_event(command, json!({}), Vec::new()))
        }
        DATAGRAM_READ => {
            let datagram = pull_until_cancelled(command.permit.session.datagram_read.clone(), async {
                connection.receive_datagram().await.map_err(|_| OpError::network())
            }).await?;
            let payload: &[u8] = datagram.as_ref();
            if payload.len() > MAX_CHUNK { return Err(OpError::quota()); }
            command.permit.session.stats.datagram_received.fetch_add(payload.len() as u64, Ordering::SeqCst);
            Ok(result_event(command, json!({"done":false}), payload.to_vec()))
        }
        CREATE_BI | ACCEPT_BI => {
            let (send, recv) = if command.operation == CREATE_BI {
                acquire_credit(connection.open_bi(), command.code == 1).await?.await.map_err(|_| OpError::network())?
            } else {
                pull_until_cancelled(command.permit.session.accept_bi.clone(), async {
                    connection.accept_bi().await.map_err(|_| OpError::network())
                }).await?
            };
            let id = install_stream(command, Some(send), Some(recv))?;
            Ok(result_event(command, json!({"streamId":id,"direction":"bidirectional"}), Vec::new()))
        }
        CREATE_UNI => {
            let send = acquire_credit(connection.open_uni(), command.code == 1).await?.await.map_err(|_| OpError::network())?;
            let id = install_stream(command, Some(send), None)?;
            Ok(result_event(command, json!({"streamId":id,"direction":"send"}), Vec::new()))
        }
        ACCEPT_UNI => {
            let recv = pull_until_cancelled(command.permit.session.accept_uni.clone(), async {
                connection.accept_uni().await.map_err(|_| OpError::network())
            }).await?;
            let id = install_stream(command, None, Some(recv))?;
            Ok(result_event(command, json!({"streamId":id,"direction":"receive"}), Vec::new()))
        }
        STREAM_READ => read_stream(command).await,
        STREAM_WRITE | STREAM_FINISH => write_stream(command).await,
        STREAM_CANCEL_READ => {
            let stream = command.stream.as_ref().ok_or_else(OpError::closed)?;
            stream.recv_cancel.send_replace(Some(command.code));
            if let Some(recv) = stream.recv.lock().await.take() { recv.stop(stream_wire_code(command.code)); }
            stream.recv_done.store(true, Ordering::SeqCst);
            Ok(result_event(command, json!({"streamId":stream.id}), Vec::new()))
        }
        STREAM_ABORT_WRITE => {
            let stream = command.stream.as_ref().ok_or_else(OpError::closed)?;
            stream.send_cancel.send_replace(Some(command.code));
            if let Some(mut send) = stream.send.lock().await.take() { let _ = send.reset(stream_wire_code(command.code)); }
            stream.send_done.store(true, Ordering::SeqCst);
            Ok(result_event(command, json!({"streamId":stream.id}), Vec::new()))
        }
        CANCEL_DATAGRAM_READ | CANCEL_ACCEPT_BI | CANCEL_ACCEPT_UNI => {
            let route = match command.operation {
                CANCEL_DATAGRAM_READ => &command.permit.session.datagram_read,
                CANCEL_ACCEPT_BI => &command.permit.session.accept_bi,
                _ => &command.permit.session.accept_uni,
            };
            route.cancel().await;
            Ok(result_event(command, json!({}), Vec::new()))
        }
        STATS => {
            let wire = connection.quic_connection().stats();
            let stats = &command.permit.session.stats;
            Ok(LumeWtEvent::new(json!({"type":"stats","sessionId":command.permit.session.id,"requestId":command.permit.request,
                "udpBytesSent":wire.udp_tx.bytes,"udpBytesReceived":wire.udp_rx.bytes,
                "datagramPayloadBytesSent":stats.datagram_sent.load(Ordering::SeqCst),
                "datagramPayloadBytesReceived":stats.datagram_received.load(Ordering::SeqCst),
                "streamPayloadBytesSent":stats.stream_sent.load(Ordering::SeqCst),
                "streamPayloadBytesReceived":stats.stream_received.load(Ordering::SeqCst)}), Vec::new()))
        }
        _ => Err(OpError::closed()),
    }
}

fn certificate_failure(error: &(dyn std::error::Error + 'static)) -> bool {
    let mut current = Some(error);
    while let Some(error) = current {
        // Inspect locally, but never export raw certificate/URL/error strings.
        if error.to_string().to_ascii_lowercase().contains("certificate") { return true; }
        current = error.source();
    }
    false
}
async fn close_streams(session: &Arc<Session>) {
    let streams: Vec<_> = lock(&session.streams).drain().map(|(_, stream)| stream).collect();
    for stream in streams {
        stream.send_cancel.send_replace(Some(0)); stream.recv_cancel.send_replace(Some(0));
        if let Some(mut send) = stream.send.lock().await.take() { let _ = send.reset(stream_wire_code(0)); }
        if let Some(recv) = stream.recv.lock().await.take() { recv.stop(stream_wire_code(0)); }
        stream.send_done.store(true, Ordering::SeqCst); stream.recv_done.store(true, Ordering::SeqCst);
    }
}
async fn session_actor(owner: Arc<Owner>, session: Arc<Session>, mut commands: mpsc::Receiver<Command>, config: Config) -> CloseInfo {
    let mut cancelled = session.cancellation.subscribe();
    let client = match config.client() {
        Ok(client) => client,
        Err(_) => return CloseInfo::failure("NetworkError", "Native TLS configuration failed"),
    };
    let endpoint = match Endpoint::client(client) {
        Ok(endpoint) => endpoint,
        Err(_) => return CloseInfo::failure("NetworkError", "Native UDP endpoint creation failed"),
    };
    let outcome = {
        // Keep the connecting future inside this scope so cancellation before
        // its first poll still releases its endpoint borrow before teardown.
        let connect = async {
            endpoint.connect(ConnectOptions::builder(&config.url).add_header("origin", &config.origin).build()).await
        };
        let already_cancelled = *cancelled.borrow();
        if already_cancelled { None } else {
            tokio::select! {
                biased;
                _ = cancelled.changed() => None,
                connection = timeout(CONNECT_TIMEOUT, connect) => Some(connection),
            }
        }
    };
    let (connection, mut finish) = match outcome {
        Some(Ok(Ok(connection))) => (Some(connection), CloseInfo::failure("NetworkError", "Native transport closed remotely")),
        Some(Ok(Err(error))) if certificate_failure(&error) => (None, CloseInfo::failure("SecurityError", "Native TLS certificate verification rejected the peer")),
        Some(Ok(Err(_))) => (None, CloseInfo::failure("NetworkError", "Native HTTP/3 session establishment failed")),
        Some(Err(_)) => (None, CloseInfo::failure("TimeoutError", "Native HTTP/3 connection deadline exceeded")),
        None => (None, lock(&session.close_info).clone().unwrap_or_else(|| CloseInfo::failure("AbortError", "Native connection was cancelled"))),
    };
    if let Some(connection) = connection {
        let h3 = connection.quic_connection().handshake_data()
            .and_then(|data| data.downcast::<wtransport::quinn::crypto::rustls::HandshakeData>().ok())
            .map(|data| data.protocol.as_deref() == Some(b"h3".as_slice())).unwrap_or(false);
        let maximum = connection.max_datagram_size();
        let may_run = !*cancelled.borrow() && owner.active().is_ok();
        if !h3 { finish = CloseInfo::failure("NetworkError", "Native handshake did not negotiate HTTP/3"); }
        else if config.require_unreliable && maximum.is_none() { finish = CloseInfo::failure("NotSupportedError", "Peer does not support unreliable datagrams"); }
        else if may_run {
            session.status.store(1, Ordering::SeqCst);
            owner.emit(LumeWtEvent::new(json!({"type":"ready","sessionId":session.id,
                "maxDatagramSize":maximum.unwrap_or(0),"alpn":"h3",
                "reliability":if maximum.is_some() {"supports-unreliable"} else {"reliable-only"}}), Vec::new()), false);
            let mut jobs = JoinSet::new();
            loop {
                if *cancelled.borrow() { break; }
                tokio::select! {
                    biased;
                    _ = cancelled.changed() => break,
                    _ = connection.closed() => break,
                    finished = jobs.join_next(), if !jobs.is_empty() => {
                        if finished.map(|result| result.is_err()).unwrap_or(false) {
                            finish = CloseInfo::failure("OperationError", "Native transport task failed"); break;
                        }
                    }
                    command = commands.recv() => match command {
                        Some(mut command) => {
                            let connection = connection.clone();
                            let task_guard = TaskGuard::new(owner.clone());
                            jobs.spawn(async move {
                                let outcome = execute(&mut command, &connection).await;
                                let event = outcome.unwrap_or_else(|error| error.event(command.permit.session.id, command.permit.request));
                                if let Some(stream) = &command.stream { command.permit.session.remove_completed_stream(stream.id); }
                                if command.permit.route.is_some() {
                                    if let Some((command, task_guard)) = publish_pull(command, task_guard, event) {
                                        // Cancellation won the publication lock. Any just-accepted
                                        // stream is reset before releasing its pending credit.
                                        discard_unpublished_stream(&command).await;
                                        let event = OpError::aborted().event(command.permit.session.id, command.permit.request);
                                        let owner = command.permit.owner.clone();
                                        drop(command); drop(task_guard); owner.emit(event, false);
                                    }
                                    return;
                                }
                                let owner = command.permit.owner.clone();
                                // Clear credits, queued payload bytes and task counts before the
                                // completion event lets JS issue another operation on this half.
                                drop(command); drop(task_guard);
                                owner.emit(event, false);
                            });
                        }
                        None => break,
                    }
                }
            }
            session.status.store(2, Ordering::SeqCst);
            jobs.abort_all();
            while jobs.join_next().await.is_some() {}
        }
        connection.close(H3_NO_ERROR, b"Lumen dedicated WebTransport endpoint closing");
    }
    commands.close(); while commands.try_recv().is_ok() {}
    close_streams(&session).await;
    endpoint.close(H3_NO_ERROR, b"Lumen document endpoint closing");
    let idle = timeout(ENDPOINT_CLOSE_TIMEOUT, endpoint.wait_idle()).await.is_ok() && endpoint.open_connections() == 0;
    if let Some(selected) = lock(&session.close_info).clone() { finish = selected; }
    if !idle { finish.clean = false; finish.error_name = "OperationError"; finish.error_message = "Native endpoint shutdown exceeded its deadline"; }
    // Endpoint is dropped before the terminal event is delivered by the outer task.
    drop(endpoint); finish
}

fn get_owner(id: Id) -> Result<Arc<Owner>, i32> {
    if id == 0 || id > MAX_SAFE_ID { return Err(INVALID); }
    lock(&engine()?.owners).get(&id).cloned().ok_or(CLOSED)
}
fn create_session(owner: Arc<Owner>, config: Config) -> Result<Id, i32> {
    let runtime = engine()?;
    let id = safe_id(&runtime.next_id).ok_or(QUOTA)?;
    let (sender, receiver) = mpsc::channel(MAX_REQUESTS);
    let (cancellation, _) = watch::channel(false);
    let session;
    {
        let mut owned = lock(&owner.sessions);
        let mut handles = lock(&owner.handles);
        owner.active()?;
        if handles.len() >= MAX_SESSIONS { return Err(QUOTA); }
        session = Arc::new(Session { id, owner: Arc::downgrade(&owner), sender,
            status: AtomicU8::new(0), cancellation, close_info: Mutex::new(None), abort: Mutex::new(None),
            requests: Mutex::new(HashSet::new()), streams: Mutex::new(HashMap::new()), stream_slots: AtomicUsize::new(0),
            datagram_read: PullRoute::new(), accept_bi: PullRoute::new(),
            accept_uni: PullRoute::new(), stats: PayloadStats::default() });
        runtime.sessions_live.fetch_add(1, Ordering::SeqCst);
        handles.insert(id);
        owned.insert(id, session.clone());
    }
    let task_owner = owner.clone(); let task_session = session.clone();
    let task_guard = TaskGuard::new(owner);
    let task = runtime.runtime.spawn(async move {
        let finish = session_actor(task_owner.clone(), task_session.clone(), receiver, config).await;
        task_session.status.store(3, Ordering::SeqCst);
        lock(&task_owner.sessions).remove(&id);
        let event = finish.event(id);
        drop(task_session); drop(task_guard);
        task_owner.emit(event, true);
    });
    *lock(&session.abort) = Some(task.abort_handle());
    Ok(id)
}
fn drop_owner(id: Id) -> Result<(), i32> {
    let owner = lock(&engine()?.owners).remove(&id).ok_or(CLOSED)?;
    ENGINE.get().unwrap().owners_live.fetch_sub(1, Ordering::SeqCst);
    owner.shutdown();
    let started = Instant::now(); let mut guard = lock(&owner.idle);
    while owner.tasks.load(Ordering::SeqCst) != 0 {
        let remaining = OWNER_CLOSE_TIMEOUT.saturating_sub(started.elapsed());
        if remaining.is_zero() {
            drop(guard);
            let sessions: Vec<_> = lock(&owner.sessions).values().cloned().collect();
            for session in sessions { if let Some(abort) = lock(&session.abort).as_ref() { abort.abort(); } }
            // The global wake callback has no context pointer, and emit remains
            // disabled even if a cancelled task is scheduled after this return.
            return Err(TIMEOUT);
        }
        guard = owner.task_changed.wait_timeout(guard, remaining).unwrap_or_else(|poisoned| poisoned.into_inner()).0;
    }
    drop(guard);
    // Covers actor cancellation: remove any dead-handle state before returning.
    lock(&owner.sessions).clear();
    Ok(())
}

fn ffi_result(operation: impl FnOnce() -> Result<(), i32>) -> i32 {
    match catch_unwind(AssertUnwindSafe(operation)) { Ok(Ok(())) => OK, Ok(Err(error)) => error, Err(_) => STATE }
}
unsafe fn input_bytes<'a>(pointer: *const u8, length: usize, maximum: usize) -> Result<&'a [u8], i32> {
    if length > maximum || (length != 0 && pointer.is_null()) { return Err(INVALID); }
    if length == 0 { Ok(&[]) } else { Ok(std::slice::from_raw_parts(pointer, length)) }
}

#[no_mangle]
pub extern "C" fn lume_wt_initialize(wake: Option<Wake>) -> i32 {
    ffi_result(|| {
        if let Some(existing) = ENGINE.get() {
            return if existing.wake.map(|callback| callback as usize) == wake.map(|callback| callback as usize) { Ok(()) } else { Err(STATE) };
        }
        let runtime = Builder::new_multi_thread().worker_threads(2).max_blocking_threads(2)
            .thread_name("lumen-wt").enable_all().build().map_err(|_| STATE)?;
        ENGINE.set(Engine { runtime, owners: Mutex::new(HashMap::new()), next_id: AtomicU64::new(1), wake,
            owners_live: AtomicUsize::new(0), sessions_live: AtomicUsize::new(0), streams_live: AtomicUsize::new(0),
            tasks_live: AtomicUsize::new(0), event_bytes: AtomicUsize::new(0), send_bytes: AtomicUsize::new(0) }).map_err(|_| STATE)
    })
}
#[no_mangle]
pub extern "C" fn lume_wt_owner_create() -> Id {
    catch_unwind(AssertUnwindSafe(|| {
        let state = engine().ok()?; let id = safe_id(&state.next_id)?;
        lock(&state.owners).insert(id, Arc::new(Owner::new(id))); Some(id)
    })).ok().flatten().unwrap_or(0)
}
#[no_mangle]
pub extern "C" fn lume_wt_owner_drop(owner: Id) -> i32 { ffi_result(|| drop_owner(owner)) }
/// # Safety
/// config and out_session must be valid for the supplied lengths and one u64 write.
#[no_mangle]
pub unsafe extern "C" fn lume_wt_connect(owner: Id, config: *const u8, length: usize, out_session: *mut Id) -> i32 {
    ffi_result(|| {
        if out_session.is_null() { return Err(INVALID); }
        *out_session = 0;
        let config = Config::parse(input_bytes(config, length, 8192)?)?;
        let session = create_session(get_owner(owner)?, config)?;
        *out_session = session; Ok(())
    })
}
/// # Safety
/// payload must reference length readable bytes, or be null when length is zero.
#[no_mangle]
pub unsafe extern "C" fn lume_wt_command(owner: Id, session: Id, stream: Id, operation: u32,
    request: Id, code: u32, payload: *const u8, length: usize) -> i32 {
    ffi_result(|| {
        let owner = get_owner(owner)?; owner.active()?;
        let session = lock(&owner.sessions).get(&session).cloned().ok_or(CLOSED)?;
        let bytes = input_bytes(payload, length, MAX_CHUNK)?;
        if operation == CLOSE {
            if stream != 0 || request != 0 || length > 1024 { return Err(INVALID); }
            let reason = std::str::from_utf8(bytes).map_err(|_| INVALID)?.to_string();
            session.shutdown(CloseInfo { clean: true, local: true, code, reason, error_name: "", error_message: "" });
            return Ok(());
        }
        let command = prepare_command(owner, session.clone(), stream, operation, request, code, bytes)?;
        session.sender.try_send(command).map_err(|error| match error {
            mpsc::error::TrySendError::Full(_) => QUOTA, mpsc::error::TrySendError::Closed(_) => CLOSED,
        })
    })
}
#[no_mangle]
pub extern "C" fn lume_wt_poll(owner: Id) -> *mut LumeWtEvent {
    catch_unwind(AssertUnwindSafe(|| {
        let owner = get_owner(owner).ok()?;
        let mut queue = lock(&owner.events);
        let event = queue.items.pop_front()?;
        let size = event.size(); queue.bytes -= size;
        ENGINE.get().unwrap().event_bytes.fetch_sub(size, Ordering::SeqCst);
        drop(queue);
        if let Some(id) = event.terminal_session { lock(&owner.handles).remove(&id); }
        Some(Box::into_raw(Box::new(event)))
    })).ok().flatten().unwrap_or(ptr::null_mut())
}
/// # Safety
/// event must be a live polled event; length must reference one writable usize.
#[no_mangle]
pub unsafe extern "C" fn lume_wt_event_json(event: *const LumeWtEvent, length: *mut usize) -> *const u8 {
    if event.is_null() || length.is_null() { return ptr::null(); }
    *length = (*event).json.len(); (*event).json.as_ptr()
}
/// # Safety
/// event must be a live polled event; length must reference one writable usize.
#[no_mangle]
pub unsafe extern "C" fn lume_wt_event_bytes(event: *const LumeWtEvent, length: *mut usize) -> *const u8 {
    if event.is_null() || length.is_null() { return ptr::null(); }
    *length = (*event).bytes.len(); (*event).bytes.as_ptr()
}
/// # Safety
/// event must be null or a live event obtained by poll, and must be freed once.
#[no_mangle]
pub unsafe extern "C" fn lume_wt_event_free(event: *mut LumeWtEvent) {
    if !event.is_null() { drop(Box::from_raw(event)); }
}
/// # Safety
/// out_counts must reference one writable LumeWtCounts.
#[no_mangle]
pub unsafe extern "C" fn lume_wt_counts(owner: Id, out_counts: *mut LumeWtCounts) -> i32 {
    ffi_result(|| {
        if out_counts.is_null() { return Err(INVALID); }
        let state = engine()?;
        let counts = if owner == 0 {
            LumeWtCounts { owners: state.owners_live.load(Ordering::SeqCst) as u64,
                sessions: state.sessions_live.load(Ordering::SeqCst) as u64, streams: state.streams_live.load(Ordering::SeqCst) as u64,
                tasks: state.tasks_live.load(Ordering::SeqCst) as u64, queued_event_bytes: state.event_bytes.load(Ordering::SeqCst) as u64,
                pending_send_bytes: state.send_bytes.load(Ordering::SeqCst) as u64 }
        } else {
            let owner = get_owner(owner)?; let sessions = lock(&owner.sessions);
            let queued_event_bytes = lock(&owner.events).bytes as u64;
            LumeWtCounts { owners: 1, sessions: sessions.len() as u64,
                streams: sessions.values().map(|session| lock(&session.streams).len() as u64).sum(),
                tasks: owner.tasks.load(Ordering::SeqCst) as u64, queued_event_bytes,
                pending_send_bytes: owner.pending_send.load(Ordering::SeqCst) as u64 }
        };
        *out_counts = counts; Ok(())
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    static SERIAL: Mutex<()> = Mutex::new(());
    static WAKES: AtomicUsize = AtomicUsize::new(0);
    extern "C" fn wake() { WAKES.fetch_add(1, Ordering::SeqCst); }
    fn initialize() { assert_eq!(lume_wt_initialize(Some(wake)), OK); }
    fn valid() -> Value { json!({"url":"https://127.0.0.1:4433/echo","origin":"http://127.0.0.1:8765"}) }
    fn parse(value: Value) -> Result<Config, i32> { Config::parse(value.to_string().as_bytes()) }
    fn counts() -> LumeWtCounts {
        let mut counts = LumeWtCounts::default();
        assert_eq!(unsafe { lume_wt_counts(0, &mut counts) }, OK); counts
    }
    // Control-plane fixtures below exercise cancellation/credits without
    // pretending to establish a network session. The FFI example is separate.
    fn session_fixture(owner: &Arc<Owner>) -> Arc<Session> {
        let (sender, _receiver) = mpsc::channel(MAX_REQUESTS);
        let (cancellation, _) = watch::channel(false);
        ENGINE.get().unwrap().sessions_live.fetch_add(1, Ordering::SeqCst);
        Arc::new(Session { id:999, owner:Arc::downgrade(owner), sender, status:AtomicU8::new(1),
            cancellation, close_info:Mutex::new(None), abort:Mutex::new(None), requests:Mutex::new(HashSet::new()),
            streams:Mutex::new(HashMap::new()), stream_slots:AtomicUsize::new(0),
            datagram_read:PullRoute::new(), accept_bi:PullRoute::new(), accept_uni:PullRoute::new(), stats:PayloadStats::default() })
    }
    #[test]
    fn immediate_credit_poll_cancels_pending_allocation_without_retry() {
        let _serial = lock(&SERIAL); initialize();
        struct PendingCredit { polls:Arc<AtomicUsize>, drops:Arc<AtomicUsize> }
        impl Future for PendingCredit {
            type Output = Result<(), ()>;
            fn poll(self:std::pin::Pin<&mut Self>, _context:&mut std::task::Context<'_>) -> Poll<Self::Output> {
                self.polls.fetch_add(1,Ordering::SeqCst); Poll::Pending
            }
        }
        impl Drop for PendingCredit { fn drop(&mut self) { self.drops.fetch_add(1,Ordering::SeqCst); } }
        let polls=Arc::new(AtomicUsize::new(0)); let drops=Arc::new(AtomicUsize::new(0));
        let result=engine().unwrap().runtime.block_on(acquire_credit(PendingCredit {polls:polls.clone(),drops:drops.clone()},false));
        assert_eq!(result.unwrap_err().name,"QuotaExceededError");
        assert_eq!(polls.load(Ordering::SeqCst),1); assert_eq!(drops.load(Ordering::SeqCst),1);
        assert_eq!(engine().unwrap().runtime.block_on(acquire_credit(std::future::ready(Ok::<_,()>(7)),false)).unwrap(),7);
    }
    #[test]
    fn cancelled_route_rejects_pending_pull_and_preserves_neighbor() {
        let _serial = lock(&SERIAL); initialize();
        engine().unwrap().runtime.block_on(async {
            let route=PullRoute::new(); let neighbor=PullRoute::new();
            let pending=pull_until_cancelled(route.clone(),std::future::pending::<Result<(),OpError>>());
            let mut pending=std::pin::pin!(pending);
            assert!(poll_fn(|context| Poll::Ready(pending.as_mut().poll(context))).await.is_pending());
            route.cancel().await;
            assert_eq!(pending.await.unwrap_err().name,"AbortError");
            assert!(!neighbor.cancelled());
            assert_eq!(pull_until_cancelled(neighbor,std::future::ready(Ok(13))).await.unwrap(),13);
        });
    }
    #[test]
    fn route_cancel_waits_for_credit_release_and_creation_codes_are_checked() {
        let _serial = lock(&SERIAL); initialize();
        let id=lume_wt_owner_create(); let owner=get_owner(id).unwrap(); let session=session_fixture(&owner);
        assert!(matches!(prepare_command(owner.clone(),session.clone(),0,CREATE_BI,1,2,&[]),Err(INVALID)));
        let command=prepare_command(owner.clone(),session.clone(),0,ACCEPT_BI,2,0,&[]).unwrap();
        assert_eq!(session.stream_slots.load(Ordering::SeqCst),1);
        engine().unwrap().runtime.block_on(async {
            let cancel=session.accept_bi.cancel(); let mut cancel=std::pin::pin!(cancel);
            assert!(poll_fn(|context| Poll::Ready(cancel.as_mut().poll(context))).await.is_pending());
            drop(command); cancel.await;
        });
        assert_eq!(session.stream_slots.load(Ordering::SeqCst),0);
        assert!(!session.accept_bi.busy.load(Ordering::SeqCst));
        assert!(matches!(prepare_command(owner.clone(),session.clone(),0,ACCEPT_BI,3,0,&[]),Err(STATE)));
        assert!(prepare_command(owner.clone(),session.clone(),0,ACCEPT_UNI,4,0,&[]).is_ok());
        drop(session); assert_eq!(lume_wt_owner_drop(id),OK);
    }
    #[test]
    fn publication_lock_prevents_result_delivery_after_route_cancel_wins() {
        let _serial = lock(&SERIAL); initialize();
        let id=lume_wt_owner_create(); let owner=get_owner(id).unwrap(); let session=session_fixture(&owner);
        let command=prepare_command(owner.clone(),session.clone(),0,DATAGRAM_READ,1,0,&[]).unwrap();
        session.datagram_read.cancellation.send_replace(true);
        let event=result_event(&command,json!({"done":false}),vec![1,2,3]);
        let unpublished=publish_pull(command,TaskGuard::new(owner.clone()),event);
        assert!(unpublished.is_some()); assert!(lume_wt_poll(id).is_null());
        drop(unpublished); assert!(!session.datagram_read.busy.load(Ordering::SeqCst));
        let command=prepare_command(owner.clone(),session.clone(),0,ACCEPT_UNI,2,0,&[]).unwrap();
        let event=result_event(&command,json!({}),Vec::new());
        assert!(publish_pull(command,TaskGuard::new(owner.clone()),event).is_none());
        engine().unwrap().runtime.block_on(session.accept_uni.cancel());
        let event=lume_wt_poll(id); assert!(!event.is_null()); unsafe {lume_wt_event_free(event);}
        drop(session); assert_eq!(lume_wt_owner_drop(id),OK);
    }
    #[test]
    fn ids_never_exceed_javascript_exact_integer_range() {
        let next = AtomicU64::new(MAX_SAFE_ID - 1);
        assert_eq!(safe_id(&next), Some(MAX_SAFE_ID - 1));
        assert_eq!(safe_id(&next), Some(MAX_SAFE_ID));
        assert_eq!(safe_id(&next), None);
    }
    #[test]
    fn url_policy_rejects_non_https_credentials_and_fragments() {
        for url in ["http://127.0.0.1:4433", "https://user:secret@127.0.0.1/", "https://127.0.0.1/#fragment", "file:///tmp/test", "data:text/plain,x"] {
            let mut value = valid(); value["url"] = json!(url); assert!(parse(value).is_err());
        }
        assert!(parse(valid()).is_ok());
    }
    #[test]
    fn origin_accepts_only_native_serialized_tuple_or_opaque_null() {
        for origin in ["https://example.test", "http://127.0.0.1:8765", "null"] {
            let mut value = valid(); value["origin"] = json!(origin); assert!(parse(value).is_ok());
        }
        for origin in ["https://user@example.test", "https://example.test/", "https://example.test/path", "https://example.test?x", "null\r\nOrigin: spoof", "file://"] {
            let mut value = valid(); value["origin"] = json!(origin); assert!(parse(value).is_err());
        }
    }
    #[test]
    fn pins_require_one_to_eight_exact_sha256_byte_arrays() {
        let mut value = valid(); value["certificateHashes"] = json!(vec![vec![0u8;32];8]); assert!(parse(value).is_ok());
        for hashes in [json!([]), json!([[1,2]]), json!(vec![vec![0u8;32];9]), json!([vec![256;32]]), json!([vec![-1;32]]), json!([vec![0.5;32]])] {
            let mut value = valid(); value["certificateHashes"] = hashes; assert!(parse(value).is_err());
        }
    }
    #[test]
    fn option_types_and_unknown_fields_do_not_silently_change_policy() {
        let mut value = valid(); value["requireUnreliable"] = json!("true"); assert!(parse(value).is_err());
        let mut value = valid(); value["headers"] = json!({"Origin":"spoof"}); assert!(parse(value).is_err());
        assert!(Config::parse(&vec![b' ';8193]).is_err());
    }
    #[test]
    fn stream_error_mapping_skips_http3_reserved_codes_and_roundtrips() {
        for code in (0..255).chain([0xffff,0xfffffff0,u32::MAX]) {
            let wire = stream_wire_code(code).into_inner();
            assert_ne!((wire - 0x21) % 0x1f, 0);
            assert_eq!(stream_application_code(wire), Some(code));
        }
        assert_eq!(stream_wire_code(0).into_inner(),0x52e4a40fa8db);
        assert_eq!(stream_wire_code(u32::MAX).into_inner(),0x52e5ac983162);
        assert_eq!(stream_application_code(0x21),None);
    }
    #[test]
    fn payload_budget_rejects_overflow_without_corrupting_accounting() {
        let budget = AtomicUsize::new(MAX_SEND_BYTES - 1);
        assert!(!reserve(&budget,2,MAX_SEND_BYTES)); assert_eq!(budget.load(Ordering::SeqCst),MAX_SEND_BYTES - 1);
        assert!(reserve(&budget,1,MAX_SEND_BYTES)); assert!(!reserve(&budget,usize::MAX,MAX_SEND_BYTES));
        assert_eq!(budget.load(Ordering::SeqCst),MAX_SEND_BYTES);
    }
    #[test]
    fn event_queue_counts_metadata_and_retains_zero_byte_payloads() {
        let _serial = lock(&SERIAL); initialize();
        let id = lume_wt_owner_create(); let owner = get_owner(id).unwrap();
        owner.emit(LumeWtEvent::new(json!({"type":"result","done":false}),Vec::new()),false);
        assert!(counts().queued_event_bytes > 0);
        let event = lume_wt_poll(id); assert!(!event.is_null());
        unsafe {
            let mut length = usize::MAX;
            let bytes = lume_wt_event_bytes(event,&mut length); assert_eq!(length,0); assert!(!bytes.is_null());
            let metadata = lume_wt_event_json(event,&mut length); assert!(length > 0); assert!(!metadata.is_null());
            lume_wt_event_free(event);
        }
        assert_eq!(counts().queued_event_bytes,0); assert_eq!(lume_wt_owner_drop(id),OK);
    }
    #[test]
    fn terminal_event_survives_a_full_metadata_queue() {
        let _serial = lock(&SERIAL); initialize();
        let id = lume_wt_owner_create(); let owner = get_owner(id).unwrap();
        for _ in 0..MAX_EVENTS { owner.emit(LumeWtEvent::new(json!({"type":"result"}),Vec::new()),false); }
        owner.emit(CloseInfo::failure("NetworkError","Closed").event(123),true);
        assert!(owner.overflowing.load(Ordering::SeqCst));
        let event = lume_wt_poll(id); assert!(!event.is_null());
        unsafe { assert_eq!(serde_json::from_slice::<Value>(&(*event).json).unwrap()["type"],"closed"); lume_wt_event_free(event); }
        owner.emit(LumeWtEvent::new(json!({"type":"result"}),Vec::new()),false);
        assert!(lume_wt_poll(id).is_null()); assert_eq!(lume_wt_owner_drop(id),OK);
    }
    #[test]
    fn earlier_terminal_event_is_retained_when_a_later_result_overflows() {
        let _serial = lock(&SERIAL); initialize();
        let id = lume_wt_owner_create(); let owner = get_owner(id).unwrap();
        owner.emit(CloseInfo::failure("NetworkError","Already closed").event(123),true);
        for _ in 1..MAX_EVENTS { owner.emit(LumeWtEvent::new(json!({"type":"result"}),Vec::new()),false); }
        owner.emit(LumeWtEvent::new(json!({"type":"result"}),Vec::new()),false);
        assert!(owner.overflowing.load(Ordering::SeqCst));
        let event = lume_wt_poll(id); assert!(!event.is_null());
        unsafe {
            let metadata: Value = serde_json::from_slice(&(*event).json).unwrap();
            assert_eq!(metadata["type"],"closed"); assert_eq!(metadata["sessionId"],123);
            lume_wt_event_free(event);
        }
        assert!(lume_wt_poll(id).is_null()); assert_eq!(lume_wt_owner_drop(id),OK);
    }
    #[test]
    fn session_quota_includes_unpolled_terminal_notifications() {
        let _serial = lock(&SERIAL); initialize();
        let id = lume_wt_owner_create(); let owner = get_owner(id).unwrap();
        lock(&owner.handles).extend([101,102,103,104]);
        assert_eq!(create_session(owner.clone(),parse(valid()).unwrap()),Err(QUOTA));
        owner.emit(CloseInfo::failure("NetworkError","Closed").event(101),true);
        let event = lume_wt_poll(id); assert!(!event.is_null()); unsafe { lume_wt_event_free(event); }
        assert_eq!(lock(&owner.handles).len(),3);
        assert_eq!(lume_wt_owner_drop(id),OK);
    }
    #[test]
    fn dropped_owner_cannot_enqueue_or_poll_late_events() {
        let _serial = lock(&SERIAL); initialize();
        let id = lume_wt_owner_create(); let owner = get_owner(id).unwrap();
        owner.emit(LumeWtEvent::new(json!({"type":"ready"}),vec![1,2]),false);
        assert_eq!(lume_wt_owner_drop(id),OK); let wakes = WAKES.load(Ordering::SeqCst);
        owner.emit(LumeWtEvent::new(json!({"type":"result"}),vec![3]),false);
        assert!(lume_wt_poll(id).is_null()); assert_eq!(WAKES.load(Ordering::SeqCst),wakes);
        let totals = counts(); assert_eq!(totals.owners,0); assert_eq!(totals.queued_event_bytes,0);
    }
    #[test]
    fn independent_document_event_ownership_survives_neighbor_drop() {
        let _serial = lock(&SERIAL); initialize();
        let first = lume_wt_owner_create(); let second = lume_wt_owner_create();
        get_owner(first).unwrap().emit(LumeWtEvent::new(json!({"type":"first"}),vec![1]),false);
        get_owner(second).unwrap().emit(LumeWtEvent::new(json!({"type":"second"}),vec![2]),false);
        assert_eq!(lume_wt_owner_drop(first),OK); assert!(lume_wt_poll(first).is_null());
        let event = lume_wt_poll(second); assert!(!event.is_null());
        unsafe { assert_eq!((*event).bytes,vec![2]); lume_wt_event_free(event); }
        assert_eq!(lume_wt_owner_drop(second),OK); assert_eq!(counts().owners,0);
    }
    #[test]
    fn c_abi_rejects_invalid_pointer_length_and_handle_inputs() {
        let _serial = lock(&SERIAL); initialize();
        let owner = lume_wt_owner_create(); let mut session = 99;
        assert_eq!(unsafe { lume_wt_connect(owner,ptr::null(),1,&mut session) },INVALID);
        assert_eq!(session,0);
        assert_eq!(unsafe { lume_wt_connect(owner,ptr::null(),0,ptr::null_mut()) },INVALID);
        assert_eq!(unsafe { lume_wt_counts(owner,ptr::null_mut()) },INVALID);
        assert!(lume_wt_poll(MAX_SAFE_ID+1).is_null());
        assert_eq!(lume_wt_owner_drop(owner),OK); assert_eq!(lume_wt_owner_drop(owner),CLOSED);
    }
}
