"""
tempmailg.com：独立 Session GET /public/{locale} + POST get_messages；Token 为 tmg1: + base64(JSON)。
"""

import base64
import json
import re
from typing import List, Optional

import requests
from urllib.parse import quote

from ..config import get_config
from ..normalize import normalize_email
from ..types import EmailInfo, Email

CHANNEL = "tempmailg"
ORIGIN = "https://tempmailg.com"
TOK_PREFIX = "tmg1:"

CSRF_RE = re.compile(r'<meta\s+name="csrf-token"\s+content="([^"]+)"', re.I)

_UA = (
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/146.0.0.0 Safari/537.36 Edg/146.0.0.0"
)


def _ephemeral_session() -> requests.Session:
    cfg = get_config()
    s = requests.Session()
    s.verify = not cfg.insecure
    if cfg.proxy:
        s.proxies = {"http": cfg.proxy, "https": cfg.proxy}
    if cfg.headers:
        s.headers.update(cfg.headers)
    return s


def _locale(domain: Optional[str]) -> str:
    s = (domain or "").strip()
    if not s or any(c in s for c in "/?#\\"):
        return "zh"
    return s


def _cookie_map(hdr: str) -> dict:
    m = {}
    for part in hdr.split(";"):
        p = part.strip()
        if "=" in p:
            k, v = p.split("=", 1)
            k, v = k.strip(), v.strip()
            if k:
                m[k] = v
    return m


def _cookie_hdr_from_map(m: dict) -> str:
    return "; ".join(f"{k}={m[k]}" for k in sorted(m.keys()))


def _merge_set_cookies(prev: str, resp: requests.Response) -> str:
    m = _cookie_map(prev)
    for c in resp.cookies.items():
        m[str(c[0])] = str(c[1])
    return _cookie_hdr_from_map(m)


def _xsrf(cookie_hdr: str) -> str:
    m = _cookie_map(cookie_hdr)
    for key in ("XSRF-TOKEN", "xsrf-token"):
        if key in m:
            return m[key]
    for k, v in m.items():
        if k.lower() == "xsrf-token":
            return v
    return ""


def _parse_csrf(html: str) -> str:
    m = CSRF_RE.search(html)
    if not m or not (t := m.group(1).strip()):
        raise RuntimeError("tempmailg: csrf-token not found in page")
    return t


def _encode_token(locale: str, cookie_hdr: str, csrf: str) -> str:
    raw = json.dumps({"l": locale, "c": cookie_hdr, "s": csrf}, separators=(",", ":"))
    return TOK_PREFIX + base64.b64encode(raw.encode("utf-8")).decode("ascii")


def _decode_token(tok: str) -> dict:
    if not tok.startswith(TOK_PREFIX):
        raise ValueError("tempmailg: invalid session token")
    raw = base64.b64decode(tok[len(TOK_PREFIX) :].encode("ascii")).decode("utf-8")
    o = json.loads(raw)
    if not o.get("c") or not o.get("s"):
        raise ValueError("tempmailg: invalid session token")
    return o


def generate_email(domain: Optional[str] = None, **kwargs) -> EmailInfo:
    loc = _locale(domain)
    page_url = f"{ORIGIN}/public/{quote(loc, safe='')}"
    cfg = get_config()
    timeout = cfg.timeout
    s = _ephemeral_session()

    r = s.get(
        page_url,
        headers={
            "User-Agent": _UA,
            "Accept": "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
            "Accept-Language": "zh-CN,zh;q=0.9,en;q=0.8",
            "Cache-Control": "no-cache",
            "DNT": "1",
            "Pragma": "no-cache",
            "Referer": page_url,
            "Upgrade-Insecure-Requests": "1",
        },
        timeout=timeout,
    )
    r.raise_for_status()
    html = r.text
    csrf = _parse_csrf(html)
    cookie_hdr = _merge_set_cookies("", r)
    xsrf = _xsrf(cookie_hdr)
    if not xsrf:
        raise RuntimeError("tempmailg: missing XSRF-TOKEN cookie")

    post_url = f"{ORIGIN}/public/get_messages"
    r2 = s.post(
        post_url,
        json={"_token": csrf},
        headers={
            "User-Agent": _UA,
            "Accept": "application/json, text/plain, */*",
            "Accept-Language": "zh-CN,zh;q=0.9,en;q=0.8",
            "Content-Type": "application/json",
            "Origin": ORIGIN,
            "Referer": page_url,
            "Cache-Control": "no-cache",
            "Pragma": "no-cache",
            "DNT": "1",
            "Cookie": cookie_hdr,
            "X-XSRF-TOKEN": xsrf,
        },
        timeout=timeout,
    )
    r2.raise_for_status()
    wrap = r2.json()
    if not wrap.get("status") or not wrap.get("mailbox"):
        raise RuntimeError("tempmailg: create mailbox failed")
    cookie_hdr = _merge_set_cookies(cookie_hdr, r2)
    tok = _encode_token(loc, cookie_hdr, csrf)
    return EmailInfo(channel=CHANNEL, email=wrap["mailbox"], _token=tok)


def get_emails(email: str, token: str) -> List[Email]:
    o = _decode_token(token)
    loc = o.get("l") or "zh"
    page_url = f"{ORIGIN}/public/{quote(str(loc), safe='')}"
    post_url = f"{ORIGIN}/public/get_messages"
    cookie_hdr = str(o["c"])
    xsrf = _xsrf(cookie_hdr)
    cfg = get_config()

    r = requests.post(
        post_url,
        json={"_token": o["s"]},
        headers={
            "User-Agent": _UA,
            "Accept": "application/json, text/plain, */*",
            "Accept-Language": "zh-CN,zh;q=0.9,en;q=0.8",
            "Content-Type": "application/json",
            "Origin": ORIGIN,
            "Referer": page_url,
            "Cache-Control": "no-cache",
            "Pragma": "no-cache",
            "DNT": "1",
            "Cookie": cookie_hdr,
            "X-XSRF-TOKEN": xsrf,
        },
        timeout=cfg.timeout,
        verify=not cfg.insecure,
        proxies={"http": cfg.proxy, "https": cfg.proxy} if cfg.proxy else None,
    )
    r.raise_for_status()
    wrap = r.json()
    if not wrap.get("status"):
        raise RuntimeError("tempmailg: get_messages failed")
    mb = (wrap.get("mailbox") or "").strip()
    if mb and mb.lower() != email.strip().lower():
        raise RuntimeError("tempmailg: mailbox mismatch")
    messages = wrap.get("messages") or []
    return [normalize_email(m, email) for m in messages if isinstance(m, dict)]
