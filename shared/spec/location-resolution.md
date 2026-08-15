# Location Resolution Contract

This document defines the shared, user-visible behavior for turning a text location into Open-Meteo latitude, longitude, and timezone values.

## Goal

- Keep Raspberry Pi runtime, Raspberry Pi setup, and ESP32-P4 setup aligned.
- Share selection rules where runtimes can reuse code directly.
- Keep HTTP/fetch/client implementations platform-specific.

## Preferred User Input

- Prefer `City, State` for US locations.
- Optional country code may be supplied as a third segment:
  - `Loveland,OH,US`
  - `Paris,FR`
- ZIP code entry is not part of the primary contract.

## Shared Search Rules

- Weather/location provider: Open-Meteo geocoding API
- Request shape:
  - endpoint: `https://geocoding-api.open-meteo.com/v1/search`
  - `name=<first segment>`
  - `count=10`
  - `language=en`
  - `format=json`
  - include `countryCode=<CC>` only when the third segment is a two-letter country code

## Shared Ranking Rules

For each returned candidate:

- exact place-name match: `+100`
- place name contains requested name: `+30`
- exact state/admin1 match: `+80`
- exact country code match: `+20`
- population bonus: `min(floor(population / 1000), 25)`

Highest score wins.

## Shared Output Rules

- `lat`: selected result latitude
- `lon`: selected result longitude
- `timezone`: selected result timezone, or `auto` if absent
- resolved label:
  - `name, admin1, country_code`
  - omit missing segments

## Code Ownership

Directly shared JavaScript logic:

- `shared/logic/open-meteo-location.js`

Directly shared validation fixtures:

- `shared/test-data/location-resolution-cases.json`

Current consumers:

- Pi runtime: `targets/pi/server.js`
- Pi setup flow: `shared/scripts/resolve-location.js`, called by `setup.sh`

Parallel implementation that must follow this contract:

- ESP32-P4 setup resolver: `targets/esp32-p4/main/location_lookup.c`

## Non-Shared Pieces

Do not try to share these across runtimes:

- HTTP client setup
- fetch timeout plumbing
- TLS/certificate handling
- JSON library bindings
- device persistence/storage

Those are platform adapters, not shared business logic.
