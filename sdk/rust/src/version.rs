/*!
 * 版本号来自 Cargo.toml（随发布 bump 自动变化）
 */
pub fn sdk_version() -> &'static str {
    env!("CARGO_PKG_VERSION")
}
