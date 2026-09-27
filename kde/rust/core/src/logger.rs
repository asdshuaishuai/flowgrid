use tracing_subscriber::{fmt, EnvFilter};

pub fn init() {
    let filter = EnvFilter::try_from_default_env().unwrap_or_else(|_| {
        EnvFilter::new("flowgrid_core=info,flowgrid_cxx_qt=info,flowgrid_app=info")
    });
    fmt().with_env_filter(filter).init();
}
