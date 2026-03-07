"""
共享 HTTP 客户端
所有 provider 通过此模块发起 HTTP 请求，自动应用全局配置（代理、超时、SSL 等）

性能优化：
- Session 内部缓存复用，仅在配置变更时重建，保证连接池复用
- 连接池大小优化（pool_connections=20, pool_maxsize=20）
- 启用 keep-alive 长连接，减少 TCP/TLS 握手开销
- 缓存 timeout 避免每次请求都读取 config
"""

import requests
from requests.adapters import HTTPAdapter
from .config import get_config, get_config_version

# 缓存的 Session 及其对应的配置版本
_cached_session = None
_cached_version = -1
_cached_timeout = 15


def get_session() -> requests.Session:
    """
    获取带全局配置的 requests.Session
    内部缓存复用，仅在配置变更时重建
    连接池配置：每个主机最多 20 个连接，总共 20 个连接
    """
    global _cached_session, _cached_version, _cached_timeout

    current_version = get_config_version()
    if _cached_session is not None and _cached_version == current_version:
        return _cached_session

    # 配置已变更或首次创建，重建 Session
    config = get_config()
    session = requests.Session()

    """
    连接池适配器：
    - pool_connections: 缓存的连接池数量（对应不同主机）
    - pool_maxsize: 每个连接池的最大连接数
    - max_retries: 底层 urllib3 级别的重试（SDK 层有自己的重试逻辑，此处设 0）
    """
    adapter = HTTPAdapter(
        pool_connections=20,
        pool_maxsize=20,
        max_retries=0,
    )
    session.mount("https://", adapter)
    session.mount("http://", adapter)

    # 代理
    if config.proxy:
        session.proxies = {
            "http": config.proxy,
            "https": config.proxy,
        }

    # SSL 验证
    session.verify = not config.insecure

    # 自定义请求头
    if config.headers:
        session.headers.update(config.headers)

    _cached_session = session
    _cached_version = current_version
    _cached_timeout = config.timeout
    return session


def get(url: str, headers: dict = None, timeout: int = None, **kwargs) -> requests.Response:
    """带全局配置的 GET 请求"""
    session = get_session()
    effective_timeout = timeout if timeout is not None else _cached_timeout
    return session.get(url, headers=headers, timeout=effective_timeout, **kwargs)


def post(url: str, headers: dict = None, timeout: int = None, **kwargs) -> requests.Response:
    """带全局配置的 POST 请求"""
    session = get_session()
    effective_timeout = timeout if timeout is not None else _cached_timeout
    return session.post(url, headers=headers, timeout=effective_timeout, **kwargs)
