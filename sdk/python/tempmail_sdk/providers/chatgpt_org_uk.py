"""
mail.chatgpt.org.uk 渠道实现
API: https://mail.chatgpt.org.uk/api
"""

from urllib.parse import quote
from .. import http as tm_http
from ..types import EmailInfo
from ..normalize import normalize_email

CHANNEL = "chatgpt-org-uk"
BASE_URL = "https://mail.chatgpt.org.uk/api"
HOME_URL = "https://mail.chatgpt.org.uk/"

DEFAULT_HEADERS = {
    "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/143.0.0.0 Safari/537.36",
    "Accept": "*/*",
    "Referer": "https://mail.chatgpt.org.uk/",
    "Origin": "https://mail.chatgpt.org.uk",
    "DNT": "1",
}


def _extract_gm_sid(resp) -> str:
    for name, value in resp.cookies.items():
        if name == "gm_sid":
            return value
    return ""


def _fetch_gm_sid_once() -> str:
    resp = tm_http.get(
        HOME_URL,
        headers=DEFAULT_HEADERS,
    )
    resp.raise_for_status()

    gm_sid = _extract_gm_sid(resp)
    if not gm_sid:
        raise Exception("Failed to extract gm_sid cookie")
    return gm_sid


def _fetch_gm_sid() -> str:
    try:
        return _fetch_gm_sid_once()
    except Exception as exc:
        msg = str(exc).lower()
        if "401" in msg or "gm_sid" in msg:
            return _fetch_gm_sid_once()
        raise


def _fetch_inbox_token_once(email: str, gm_sid: str) -> str:
    resp = tm_http.post(
        f"{BASE_URL}/inbox-token",
        headers={
            **DEFAULT_HEADERS,
            "Content-Type": "application/json",
            "Cookie": f"gm_sid={gm_sid}",
        },
        json={"email": email},
    )
    resp.raise_for_status()
    data = resp.json()
    token = data.get("auth", {}).get("token")
    if not token:
        raise Exception("Failed to get inbox token")
    return token


def _fetch_inbox_token(email: str) -> str:
    gm_sid = _fetch_gm_sid()
    try:
        return _fetch_inbox_token_once(email, gm_sid)
    except Exception as exc:
        if "401" in str(exc):
            gm_sid = _fetch_gm_sid()
            return _fetch_inbox_token_once(email, gm_sid)
        raise



def generate_email(**kwargs) -> EmailInfo:
    """创建临时邮箱"""
    resp = tm_http.get(
        f"{BASE_URL}/generate-email",
        headers=DEFAULT_HEADERS,
    )
    resp.raise_for_status()
    data = resp.json()

    if not data.get("success"):
        raise Exception("Failed to generate email")

    email = data["data"]["email"]
    token = _fetch_inbox_token(email)

    return EmailInfo(
        channel=CHANNEL,
        email=email,
        _token=token,
    )



def get_emails(token: str, email: str, **kwargs) -> list:
    """获取邮件列表"""
    if not token:
        raise Exception("internal error: token missing for chatgpt-org-uk")

    def _fetch_emails(token_value: str) -> list:
        resp = tm_http.get(
            f"{BASE_URL}/emails?email={quote(email)}",
            headers={
                **DEFAULT_HEADERS,
                "x-inbox-token": token_value,
            },
        )
        resp.raise_for_status()
        data = resp.json()

        if not data.get("success"):
            raise Exception("Failed to get emails")

        return [normalize_email(raw, email) for raw in (data.get("data", {}).get("emails") or [])]

    try:
        return _fetch_emails(token)
    except Exception as exc:
        if "401" in str(exc):
            refreshed_token = _fetch_inbox_token(email)
            return _fetch_emails(refreshed_token)
        raise
