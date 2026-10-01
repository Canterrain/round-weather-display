// Shared on-screen keyboard for the WiFi and Settings screens.
//
// Built from scratch rather than relying on an OS-level virtual keyboard
// (e.g. squeekboard): Electron kiosk windows don't reliably trigger those,
// and this keeps the feature as self-contained as the rest of the project,
// matching how the ESP32-P4's LVGL keyboard is fully self-contained.

const OSK_ROWS_LETTERS = [
  ['1', '2', '3', '4', '5', '6', '7', '8', '9', '0'],
  ['q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p'],
  ['a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l'],
  ['shift', 'z', 'x', 'c', 'v', 'b', 'n', 'm', 'backspace']
];

const OSK_ROWS_SYMBOLS = [
  ['1', '2', '3', '4', '5', '6', '7', '8', '9', '0'],
  ['!', '@', '#', '$', '%', '^', '&', '*', '(', ')'],
  ['-', '_', '=', '+', '.', ',', '?', '/', ':'],
  ['shift', '~', '`', '\'', '"', ';', 'backspace']
];

// Renders a keyboard into `container` that types into whatever input
// `getInput()` returns. Returns controls for the owning screen.
function createOnScreenKeyboard(container, getInput) {
  const state = { shiftActive: false, symbolsActive: false };

  function keyLabel(key) {
    if (key === 'shift') return '⇧';
    if (key === 'backspace') return '⌫';
    if (state.shiftActive && key.length === 1 && /[a-z]/.test(key)) return key.toUpperCase();
    return key;
  }

  function press(key) {
    const input = getInput();
    if (!input) return;

    if (key === 'shift') {
      state.shiftActive = !state.shiftActive;
      build();
      return;
    }

    if (key === 'backspace') {
      input.value = input.value.slice(0, -1);
      return;
    }

    if (input.maxLength > 0 && input.value.length >= input.maxLength) return;

    input.value += state.shiftActive && /[a-z]/.test(key) ? key.toUpperCase() : key;

    if (state.shiftActive) {
      state.shiftActive = false;
      build();
    }
  }

  function makeKey(text, className, onClick) {
    const button = document.createElement('button');
    button.type = 'button';
    button.className = className;
    button.textContent = text;
    button.addEventListener('click', onClick);
    return button;
  }

  function build() {
    container.innerHTML = '';
    const rows = state.symbolsActive ? OSK_ROWS_SYMBOLS : OSK_ROWS_LETTERS;

    rows.forEach((rowKeys) => {
      const row = document.createElement('div');
      row.className = 'osk-row';
      rowKeys.forEach((key) => {
        const wide = key === 'shift' || key === 'backspace';
        row.appendChild(makeKey(keyLabel(key), wide ? 'osk-key osk-key-wide' : 'osk-key', () => press(key)));
      });
      container.appendChild(row);
    });

    const bottomRow = document.createElement('div');
    bottomRow.className = 'osk-row';
    bottomRow.appendChild(makeKey(state.symbolsActive ? 'ABC' : '123', 'osk-key osk-key-wide', () => {
      state.symbolsActive = !state.symbolsActive;
      build();
    }));
    bottomRow.appendChild(makeKey('space', 'osk-key osk-key-space', () => press(' ')));
    container.appendChild(bottomRow);
  }

  build();

  return {
    reset() {
      state.shiftActive = false;
      state.symbolsActive = false;
      build();
    },
    setDisabled(disabled) {
      container.style.pointerEvents = disabled ? 'none' : '';
      container.style.opacity = disabled ? '0.5' : '';
    }
  };
}

window.createOnScreenKeyboard = createOnScreenKeyboard;
