# Weather-logic parity tests

The ESP32-P4 firmware can't run JavaScript, so the shared business logic in
`shared/logic/` (the forecast-representative heuristic, the geocoding
result scorer) is hand-ported to C:

- `shared/logic/forecast-representative.js` &harr; `main/forecast_representative.c`
- `shared/logic/open-meteo-location.js` &harr; `main/location_scoring.c`

Both C ports are deliberately free of ESP-IDF dependencies (only `cJSON` +
the C standard library), so they compile natively on a dev machine. This
directory tests them against the *exact same* JSON fixtures the JS side is
tested against:

- `shared/test-data/forecast-representative-cases.json`
- `shared/test-data/location-resolution-cases.json`

If the JS and C implementations ever disagree on a fixture case, one of the
two test runs fails loudly. That's the whole point: this is a
development-time / CI-time check, not something an end user ever sees. If
you change the heuristic or the scorer on one side, run both suites before
shipping:

```bash
npm run test:forecast          # JS, targets/pi/scripts/validate-forecast-representative.js
npm run test:location          # JS, shared/scripts/validate-location-resolution.js
npm run test:esp32-parity      # C, this directory
npm run test:all               # all three
```

`test:esp32-parity` compiles with plain `cc` -- no ESP-IDF toolchain
needed, no board required. It reuses the cJSON copy already vendored in
`.esp-idf/esp-idf-v5.5.5/components/json/cJSON/` rather than vendoring a
second copy, so ESP-IDF does need to be installed first
(`targets/esp32-p4/scripts/setup.sh`).

## macOS: "xcode-select" / broken `cc`

If `cc` fails with something like `xcodebuild ... failed` or `Failed to
locate 'clang'`, that's a broken Xcode Command Line Tools selection on your
Mac -- unrelated to this project. Either fix it (`sudo xcode-select
--reset`, or reinstall the Command Line Tools), or point the script at the
CommandLineTools clang directly for one run:

```bash
CC=/Library/Developer/CommandLineTools/usr/bin/clang npm run test:esp32-parity
```

## Adding a new fixture case

Add it to the relevant JSON file in `shared/test-data/` -- both the JS test
and this C test read the same file, so one new fixture exercises both
implementations. No need to duplicate a case by hand in two languages.
