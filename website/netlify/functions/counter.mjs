// Besucherzähler (Netlify Function v2 + Netlify Blobs)
// Eindeutige Besucher pro Tag: SHA-256(Tages-Salt + IP + User-Agent). Die IP wird nie gespeichert,
// Salt und Hashes des Vortags werden beim ersten Aufruf eines neuen Tages gelöscht.
import { getStore } from '@netlify/blobs';

// Crawler und Link-Vorschauen nicht mitzählen
const BOT_RE = /bot|crawl|spider|slurp|preview|monitor|curl|wget|python|headless/i;

const sha256Hex = async (text) => {
  const buf = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(text));
  return [...new Uint8Array(buf)].map((b) => b.toString(16).padStart(2, '0')).join('');
};

// Salt des heutigen Tages (UTC) holen; bei Tageswechsel neuen Salt erzeugen und alte Hashes löschen
const getDailySalt = async (store, day) => {
  const cur = await store.get('salt', { type: 'json' });
  if (cur && cur.day === day) return cur.salt;
  const salt = crypto.randomUUID();
  await store.setJSON('salt', { day, salt });
  const { blobs } = await store.list({ prefix: 'v/' });
  await Promise.all(blobs.filter((b) => !b.key.startsWith(`v/${day}/`)).map((b) => store.delete(b.key)));
  return salt;
};

export default async (req, context) => {
  if (req.method !== 'POST') {
    return new Response('Method Not Allowed', { status: 405 });
  }
  const store = getStore({ name: 'counter', consistency: 'strong' });
  const ua = req.headers.get('user-agent') ?? '';
  let count = Number(await store.get('visitors')) || 0;

  if (context.ip && ua && !BOT_RE.test(ua)) {
    const day = new Date().toISOString().slice(0, 10);
    const salt = await getDailySalt(store, day);
    const key = `v/${day}/${await sha256Hex(salt + context.ip + ua)}`;
    if ((await store.get(key)) === null) {
      // Nicht atomar: gleichzeitige Aufrufe können sich überschreiben – für einen Zähler vertretbar
      await store.set(key, '1');
      count += 1;
      await store.set('visitors', String(count));
    }
  }
  return Response.json({ count }, { headers: { 'Cache-Control': 'no-store' } });
};

export const config = { path: '/api/counter' };
