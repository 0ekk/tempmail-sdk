import { InternalEmailInfo, Email, Channel } from '../types';
import { normalizeEmail } from '../normalize';
import { fetchWithTimeout } from '../retry';

const CHANNEL: Channel = 'minmail';
const BASE_URL = 'https://minmail.app/api';

const DEFAULT_HEADERS = {
  'Accept': '*/*',
  'Accept-Language': 'zh-CN,zh;q=0.9,en;q=0.8,en-GB;q=0.7,en-US;q=0.6,zh-TW;q=0.5',
  'Origin': 'https://minmail.app',
  'Referer': 'https://minmail.app/cn',
  'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/146.0.0.0 Safari/537.36 Edg/146.0.0.0',
  'sec-ch-ua': '"Chromium";v="146", "Not-A.Brand";v="24", "Microsoft Edge";v="146"',
  'sec-ch-ua-mobile': '?0',
  'sec-ch-ua-platform': '"Windows"',
  'sec-fetch-dest': 'empty',
  'sec-fetch-mode': 'cors',
  'sec-fetch-site': 'same-origin',
};

interface MinmailAddressResponse {
  address: string;
  expire: number;
  remainingTime: number;
}

interface MinmailMessage {
  id: string;
  from: string;
  to: string;
  subject: string;
  preview: string;
  content: string;
  date: string;
  isRead: boolean;
}

interface MinmailListResponse {
  message: MinmailMessage[];
}

function randomString(length: number): string {
  const chars = 'abcdefghijklmnopqrstuvwxyz0123456789';
  let result = '';
  for (let i = 0; i < length; i++) {
    result += chars[Math.floor(Math.random() * chars.length)];
  }
  return result;
}

function generateVisitorId(): string {
  return `${randomString(8)}-${randomString(4)}-${randomString(4)}-${randomString(4)}-${randomString(12)}`;
}

export async function generateEmail(): Promise<InternalEmailInfo> {
  const visitorId = generateVisitorId();
  const gaId = `GA1.1.${Date.now()}.${Math.floor(Math.random() * 1000000)}`;

  const headers = {
    ...DEFAULT_HEADERS,
    'visitor-id': visitorId,
    'Cookie': `_ga=GA1.1.${gaId}; _ga_DFGB8WF1WG=GS2.1.s${Date.now()}$o1$g0$t${Date.now()}$j60$l0$h0`,
  };

  const response = await fetchWithTimeout(`${BASE_URL}/mail/address?refresh=true&expire=1440&part=main`, {
    method: 'GET',
    headers,
  });

  if (!response.ok) {
    throw new Error(`Failed to generate email: ${response.status}`);
  }

  const data: MinmailAddressResponse = await response.json();

  return {
    channel: CHANNEL,
    email: data.address,
    token: visitorId,
    expiresAt: Date.now() + (data.expire * 60 * 1000),
  };
}

export async function getEmails(email: string, token?: string): Promise<Email[]> {
  const visitorId = token || generateVisitorId();
  const gaId = `GA1.1.${Date.now()}.${Math.floor(Math.random() * 1000000)}`;

  const headers = {
    ...DEFAULT_HEADERS,
    'visitor-id': visitorId,
    'Cookie': `_ga=GA1.1.${gaId}; _ga_DFGB8WF1WG=GS2.1.s${Date.now()}$o1$g0$t${Date.now()}$j60$l0$h0`,
  };

  const response = await fetchWithTimeout(`${BASE_URL}/mail/list?part=main`, {
    method: 'GET',
    headers,
  });

  if (!response.ok) {
    throw new Error(`Failed to get emails: ${response.status}`);
  }

  const data: MinmailListResponse = await response.json();
  const messages = data.message || [];

  return messages
    .filter((m: MinmailMessage) => m.to === email)
    .map((raw: MinmailMessage) => normalizeEmail({
      id: raw.id,
      from: raw.from || '',
      to: raw.to || email,
      subject: raw.subject || '',
      text: raw.preview || '',
      html: raw.content || '',
      date: raw.date || '',
      isRead: raw.isRead || false,
    }, email));
}