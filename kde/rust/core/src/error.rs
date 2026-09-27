use thiserror::Error;

#[derive(Error, Debug, Clone)]
pub enum FlowGridError {
    #[error("Protocol FFI error: {0}")]
    ProtocolFfi(String),
    #[error("Transport error: {0}")]
    Transport(String),
    #[error("HAL error: {0}")]
    Hal(String),
    #[error("Config error: {0}")]
    Config(String),
    #[error("IO error: {0}")]
    Io(String),
    #[error("Permission denied: {0}")]
    PermissionDenied(String),
    #[error("Device not found: {0}")]
    DeviceNotFound(String),
    #[error("Connection lost")]
    ConnectionLost,
    #[error("Sequence gap detected")]
    SequenceGap,
    #[error("Invalid payload")]
    InvalidPayload,
    #[error("Version mismatch")]
    VersionMismatch,
    #[error("Identify rejected")]
    IdentifyRejected,
    #[error("Not connected")]
    NotConnected,
    #[error("Already connected")]
    AlreadyConnected,
    #[error("Unknown")]
    Unknown,
}

impl FlowGridError {
    pub fn protocol_ffi(msg: impl Into<String>) -> Self {
        Self::ProtocolFfi(msg.into())
    }

    pub fn transport(msg: impl Into<String>) -> Self {
        Self::Transport(msg.into())
    }

    pub fn hal(msg: impl Into<String>) -> Self {
        Self::Hal(msg.into())
    }

    pub fn config(msg: impl Into<String>) -> Self {
        Self::Config(msg.into())
    }

    pub fn io(msg: impl Into<String>) -> Self {
        Self::Io(msg.into())
    }
}

pub type Result<T> = std::result::Result<T, FlowGridError>;
