import { InternalEmailInfo, Email, Channel } from '../types';
import { normalizeEmail } from '../normalize';
import { fetchWithTimeout } from '../retry';

const CHANNEL: Channel = 'temporary-email-org';
const MESSAGES_URL = 'https://www.temporary-email.org/zh/messages';
const REFERER = 'https://www.temporary-email.org/zh';

const BASE_HEADERS: Record<string, string> = {
  'User-Agent':
    'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/146.0.0.0 Safari/537.36 Edg/146.0.0.0',
  Accept: 'text/plain, */*; q=0.01',
  'accept-language': 'zh-CN,zh;q=0.9,en;q=0.8,en-GB;q=0.7,en-US;q=0.6',
  'cache-control': 'no-cache',
  dnt: '1',
  pragma: 'no-cache',
  priority: 'u=1, i',
  referer: REFERER,
  'sec-ch-ua': '"Chromium";v="146", "Not-A.Brand";v="24", "Microsoft Edge";v="146"',
  'sec-ch-ua-mobile': '?0',
  'sec-ch-ua-platform': '"Windows"',
  'sec-fetch-dest': 'empty',
  'sec-fetch-mode': 'cors',
  'sec-fetch-site': 'same-origin',
};

function cookieHeaderFromResponse(response: Response): string {
  const h = response.headers as Headers & { getSetCookie?: () => string[] };
  if (typeof h.getSetCookie === 'function') {
    const lines = h.getSetCookie();
    const pairs = lines
      .map(line => line.split(';')[0].trim())
      .filter(Boolean);
    return pairs.join('; ');
  }
  const raw = response.headers.get('set-cookie');
  if (!raw) return '';
  return raw
    .split(/,(?=\s*[A-Za-z_][\w-]*=)/)
    .map(part => part.split(';')[0].trim())
    .filter(Boolean)
    .join('; ');
}

interface MessagesJson {
  mailbox?: string;
  messages?: unknown[];
}

function parseMessagesBody(text: string): MessagesJson {
  try {
    return JSON.parse(text) as MessagesJson;
  } catch {
    return {};
  }
}

export async function generateEmail(): Promise<InternalEmailInfo> {
  const response = await fetchWithTimeout(MESSAGES_URL, {
    method: 'GET',
    headers: BASE_HEADERS,
  });

  if (!response.ok) {
    throw new Error(`temporary-email-org: create failed: ${response.status}`);
  }

  const cookie = cookieHeaderFromResponse(response);
  if (!cookie || !cookie.includes('temporaryemail_session=') || !cookie.includes('email=')) {
    throw new Error('temporary-email-org: missing session cookies');
  }

  const data = parseMessagesBody(await response.text());
  const mailbox = typeof data.mailbox === 'string' ? data.mailbox.trim() : '';
  if (!mailbox || !mailbox.includes('@')) {
    throw new Error('temporary-email-org: invalid mailbox in response');
  }

  return {
    channel: CHANNEL,
    email: mailbox,
    token: cookie,
  };
}

export async function getEmails(_email: string, cookieHeader: string): Promise<Email[]> {
  const response = await fetchWithTimeout(MESSAGES_URL, {
    method: 'GET',
    headers: {
      ...BASE_HEADERS,
      'x-requested-with': 'XMLHttpRequest',
      Cookie: cookieHeader,
    },
  });

  if (!response.ok) {
    throw new Error(`temporary-email-org: get messages failed: ${response.status}`);
  }

  const data = parseMessagesBody(await response.text());
  const list = Array.isArray(data.messages) ? data.messages : [];
  return list.map((raw: any) => normalizeEmail(raw, _email));
}
