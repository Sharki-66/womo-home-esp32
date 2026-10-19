// Seitenaufrufzähler (Netlify Function v2 + Netlify Blobs)
// Gespeichert wird nur die Gesamtzahl – keine IP, keine Uhrzeit, kein User-Agent.
import { getStore } from '@netlify/blobs';

export default async (req) => {
  if (req.method !== 'POST') {
    return new Response('Method Not Allowed', { status: 405 });
  }
  const store = getStore({ name: 'counter', consistency: 'strong' });
  // Nicht atomar: gleichzeitige Aufrufe können sich überschreiben – für einen Zähler vertretbar
  const current = Number(await store.get('pageviews')) || 0;
  const count = current + 1;
  await store.set('pageviews', String(count));
  return Response.json({ count }, { headers: { 'Cache-Control': 'no-store' } });
};

export const config = { path: '/api/counter' };
