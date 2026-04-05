/**
 * tempmailg.com：无 Cookie 罐 GET /public/{locale} 拿会话，POST /public/get_messages 建邮与收信。
 * Token 为 tmg1: + base64(JSON{locale,cookieHdr,csrf})。
 */
import { InternalEmailInfo, Email, Channel } from '../types';
import { normalizeEmail } from '../normalize';
import { fetchWithTimeout } from '../retry';

const CHANNEL: Channel = 'tempmailg';
const ORIGIN = 'https://tempmailg.com';
const TOK_PREFIX = 'tmg1:';

const CSRF_META_RE = /<meta\s+name="csrf-token"\s+content="([^"]+)"/i;

const BROWSER_UA =
  'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/146.0.0.0 Safari/537.36 Edg/146.0.0.0';

function localeFromDomain(domain?: string | null): string {
  const s = String(domain ?? '').trim();
  if (!s || /[/?#\\]/.test(s)) return 'zh';
  return s;
}

function setCookieLines(headers: Headers): string[] {
  const h = headers as Headers & { getSetCookie?: () => string[] };
  if (typeof h.getSetCookie === 'function') {
    return h.getSetCookie();
  }
  const one = headers.get('set-cookie');
  return one ? [one] : [];
}

function cookieMap(cookieHdr: string): Map<string, string> {
  const m = new Map<string, string>();
  for (const part of cookieHdr.split(';')) {
    const p = part.trim();
    if (!p) continue;
    const i = p.indexOf('=');
    if (i <= 0 || i >= p.length - 1) continue;
    const k = p.slice(0, i).trim();
    const v = p.slice(i + 1).trim();
    if (k) m.set(k, v);
  }
  return m;
}

function mergeCookies(prev: string, setCookieLinesFromResp: string[]): string {
  const m = cookieMap(prev);
  for (const line of setCookieLinesFromResp) {
    const nv = line.split(';')[0]?.trim();
    if (!nv) continue;
    const i = nv.indexOf('=');
    if (i <= 0) continue;
    const k = nv.slice(0, i).trim();
    const v = nv.slice(i + 1).trim();
    if (k) m.set(k, v);
  }
  return [...m.entries()]
    .sort(([a], [b]) => a.localeCompare(b))
    .map(([k, v]) => `${k}=${v}`)
    .join('; ');
}

function xsrfFromCookies(cookieHdr: string): string {
  const m = cookieMap(cookieHdr);
  for (const name of ['XSRF-TOKEN', 'xsrf-token']) {
    const v = m.get(name);
    if (v) return v;
  }
  for (const [k, v] of m) {
    if (k.toLowerCase() === 'xsrf-token' && v) return v;
  }
  return '';
}

function parseCsrf(html: string): string {
  const m = html.match(CSRF_META_RE);
  const t = m?.[1]?.trim() ?? '';
  if (!t) throw new Error('tempmailg: csrf-token not found in page');
  return t;
}

function b64EncodeJson(obj: { l: string; c: string; s: string }): string {
  const json = JSON.stringify(obj);
  if (typeof Buffer !== 'undefined') {
    return TOK_PREFIX + Buffer.from(json, 'utf8').toString('base64');
  }
  return TOK_PREFIX + btoa(unescape(encodeURIComponent(json)));
}

function decodeToken(tok: string): { l: string; c: string; s: string } {
  if (!tok.startsWith(TOK_PREFIX)) {
    throw new Error('tempmailg: invalid session token');
  }
  const raw = tok.slice(TOK_PREFIX.length);
  let json: string;
  if (typeof Buffer !== 'undefined') {
    json = Buffer.from(raw, 'base64').toString('utf8');
  } else {
    json = decodeURIComponent(escape(atob(raw)));
  }
  const o = JSON.parse(json) as { l?: string; c?: string; s?: string };
  if (!o.c || !o.s) throw new Error('tempmailg: invalid session token');
  return { l: o.l || 'zh', c: o.c, s: o.s };
}

function pageHeaders(referer: string): Record<string, string> {
  return {
    'User-Agent': BROWSER_UA,
    Accept: 'text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8',
    'Accept-Language': 'zh-CN,zh;q=0.9,en;q=0.8',
    'Cache-Control': 'no-cache',
    DNT: '1',
    Pragma: 'no-cache',
    Referer: referer,
    'Upgrade-Insecure-Requests': '1',
  };
}

function apiHeaders(referer: string, cookieHdr: string, xsrf: string): Record<string, string> {
  const h: Record<string, string> = {
    'User-Agent': BROWSER_UA,
    Accept: 'application/json, text/plain, */*',
    'Accept-Language': 'zh-CN,zh;q=0.9,en;q=0.8',
    'Content-Type': 'application/json',
    Origin: ORIGIN,
    Referer: referer,
    'Cache-Control': 'no-cache',
    Pragma: 'no-cache',
    DNT: '1',
    Cookie: cookieHdr,
  };
  if (xsrf) h['X-XSRF-TOKEN'] = xsrf;
  return h;
}

export async function generateEmail(domain?: string | null): Promise<InternalEmailInfo> {
  const locale = localeFromDomain(domain);
  const pageURL = `${ORIGIN}/public/${encodeURIComponent(locale)}`;

  const res = await fetchWithTimeout(pageURL, { headers: pageHeaders(pageURL) });
  if (!res.ok) {
    throw new Error(`tempmailg page: ${res.status}`);
  }
  const html = await res.text();
  const csrf = parseCsrf(html);
  let cookieHdr = mergeCookies('', setCookieLines(res.headers));
  let xsrf = xsrfFromCookies(cookieHdr);
  if (!xsrf) {
    throw new Error('tempmailg: missing XSRF-TOKEN cookie');
  }

  const postURL = `${ORIGIN}/public/get_messages`;
  const res2 = await fetchWithTimeout(postURL, {
    method: 'POST',
    headers: apiHeaders(pageURL, cookieHdr, xsrf),
    body: JSON.stringify({ _token: csrf }),
  });
  if (!res2.ok) {
    throw new Error(`tempmailg get_messages: ${res2.status}`);
  }
  const wrap = (await res2.json()) as { status?: boolean; mailbox?: string; messages?: unknown[] };
  if (!wrap.status || !wrap.mailbox) {
    throw new Error('tempmailg: create mailbox failed');
  }
  cookieHdr = mergeCookies(cookieHdr, setCookieLines(res2.headers));
  const xsrf2 = xsrfFromCookies(cookieHdr);
  if (xsrf2) xsrf = xsrf2;

  const token = b64EncodeJson({ l: locale, c: cookieHdr, s: csrf });
  return {
    channel: CHANNEL,
    email: wrap.mailbox,
    token,
  };
}

export async function getEmails(email: string, token: string): Promise<Email[]> {
  const sess = decodeToken(token);
  const locale = sess.l || 'zh';
  const pageURL = `${ORIGIN}/public/${encodeURIComponent(locale)}`;
  const postURL = `${ORIGIN}/public/get_messages`;
  const xsrf = xsrfFromCookies(sess.c);

  const res = await fetchWithTimeout(postURL, {
    method: 'POST',
    headers: apiHeaders(pageURL, sess.c, xsrf),
    body: JSON.stringify({ _token: sess.s }),
  });
  if (!res.ok) {
    throw new Error(`tempmailg get_messages: ${res.status}`);
  }
  const wrap = (await res.json()) as { status?: boolean; mailbox?: string; messages?: unknown[] };
  if (!wrap.status) {
    throw new Error('tempmailg: get_messages failed');
  }
  if (wrap.mailbox && wrap.mailbox.trim().toLowerCase() !== email.trim().toLowerCase()) {
    throw new Error('tempmailg: mailbox mismatch');
  }
  const rawList = Array.isArray(wrap.messages) ? wrap.messages : [];
  const out: Email[] = [];
  for (const rm of rawList) {
    if (rm && typeof rm === 'object') {
      out.push(normalizeEmail(rm as Record<string, unknown>, email));
    }
  }
  return out;
}
