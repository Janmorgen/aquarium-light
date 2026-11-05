# Control surface

A small service that hosts the aquarium light's control page off-device, on the
homelab box.

The light already serves its own page. This one adds what an ESP32 cannot: it
**records history**. The device has no storage and its RAM is spoken for, so it
can only ever answer "what am I doing now" — this answers "what has it been
doing", including whether it was reachable at all.

It also keeps the device credentials off the browser. The page is same-origin
and this process injects basic auth on the way through.

## Running it

```sh
echo 'LIGHT_PASS=<the light WEB_PASSWORD>' > .env
docker compose up -d
```

Then open `http://<host>:8181/`.

Standard library only — no pip install, no image build, no network dependency at
container start. `app/` is mounted read-only, so changing the page is an edit
and a `docker compose restart`.

## What it serves

| Path | Does |
| --- | --- |
| `/` | The control page |
| `/api/status`, `/api/frame` | Proxied from the light; `{"reachable":false}` if it is down |
| `/api/history?hours=N` | Recorded samples, downsampled to ~700 points |
| `POST /api/*` | Proxied to the light with its credentials attached |

An unreachable light is reported in-band rather than as a 502, because "the
light is down" is information the page draws rather than an error it hits.

## History

A background thread polls `/api/status` every `POLL_SECONDS` into SQLite at
`DB_PATH`, keeping `RETAIN_DAYS`. Failed polls are recorded as samples with
`reachable = 0` — the gaps are the point.

The mode timeline downsamples, so a very short-lived mode can be missed on long
ranges; `/api/status` is the authority on the present. **Outages are exempt** —
both edges of every reachability change survive downsampling, because a dropped
gap would read as uptime, and uptime is the question this exists to answer.
