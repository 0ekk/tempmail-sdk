"""
temporary-email.org 渠道
首次 GET /zh/messages 下发 Cookie；收信需 Cookie + x-requested-with: XMLHttpRequest
"""

from .. import http as tm_http
from ..types import EmailInfo
from ..normalize import normalize_email

CHANNEL = "temporary-email-org"
MESSAGES_URL = "https://www.temporary-email.org/zh/messages"
REFERER = "https://www.temporary-email.org/zh"

DEFAULT_HEADERS = {
    "User-Agent": (
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
        "Chrome/146.0.0.0 Safari/537.36 Edg/146.0.0.0"
    ),
    "Accept": "text/plain, */*; q=0.01",
    "accept-language": "zh-CN,zh;q=0.9,en;q=0.8,en-GB;q=0.7,en-US;q=0.6",
    "cache-control": "no-cache",
    "dnt": "1",
    "pragma": "no-cache",
    "priority": "u=1, i",
    "referer": REFERER,
    "sec-ch-ua": '"Chromium";v="146", "Not-A.Brand";v="24", "Microsoft Edge";v="146"',
    "sec-ch-ua-mobile": "?0",
    "sec-ch-ua-platform": '"Windows"',
    "sec-fetch-dest": "empty",
    "sec-fetch-mode": "cors",
    "sec-fetch-site": "same-origin",
}


def _cookie_header_from_response(resp) -> str:
    parts = []
    for c in resp.cookies.items():
        parts.append(f"{c[0]}={c[1]}")
    return "; ".join(parts)


def generate_email(**kwargs) -> EmailInfo:
    resp = tm_http.get(MESSAGES_URL, headers=DEFAULT_HEADERS)
    resp.raise_for_status()

    cookie = _cookie_header_from_response(resp)
    if not cookie or "temporaryemail_session=" not in cookie or "email=" not in cookie:
        raise RuntimeError("temporary-email-org: missing session cookies")

    data = resp.json()
    mailbox = (data.get("mailbox") or "").strip()
    if not mailbox or "@" not in mailbox:
        raise RuntimeError("temporary-email-org: invalid mailbox")

    return EmailInfo(channel=CHANNEL, email=mailbox, _token=cookie)


def get_emails(token: str, email: str = "", **kwargs) -> list:
    resp = tm_http.get(
        MESSAGES_URL,
        headers={
            **DEFAULT_HEADERS,
            "Cookie": token,
            "x-requested-with": "XMLHttpRequest",
        },
    )
    resp.raise_for_status()
    data = resp.json()
    raw_list = data.get("messages") or []
    return [normalize_email(raw, email) for raw in raw_list]
