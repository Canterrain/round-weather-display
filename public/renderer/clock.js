const SVG_NS = 'http://www.w3.org/2000/svg';
const DIAL_CENTER = 380;
const SECOND_HAND_ENABLED = true;

function polarPoint(angleDeg, radius) {
  const angleRad = (angleDeg - 90) * (Math.PI / 180);
  return {
    x: DIAL_CENTER + Math.cos(angleRad) * radius,
    y: DIAL_CENTER + Math.sin(angleRad) * radius
  };
}

function createSvgElement(name, attrs = {}) {
  const el = document.createElementNS(SVG_NS, name);
  Object.entries(attrs).forEach(([key, value]) => {
    el.setAttribute(key, String(value));
  });
  return el;
}

function ensureDialDefs(svg) {
  if (!svg || svg.querySelector('defs')) return;

  const defs = createSvgElement('defs');

  const capGradient = createSvgElement('radialGradient', {
    id: 'center-cap-gradient',
    cx: '35%',
    cy: '35%',
    r: '70%'
  });
  capGradient.appendChild(createSvgElement('stop', { offset: '0%', 'stop-color': '#edf2f8' }));
  capGradient.appendChild(createSvgElement('stop', { offset: '55%', 'stop-color': '#c3ccd9' }));
  capGradient.appendChild(createSvgElement('stop', { offset: '100%', 'stop-color': '#7d8796' }));

  defs.appendChild(capGradient);
  svg.prepend(defs);
}

function buildDialSvg() {
  const faceSvg = document.getElementById('dial-svg');
  const handsSvg = document.getElementById('dial-hands-svg');
  const ticksGroup = document.getElementById('dial-ticks');
  const numeralsGroup = document.getElementById('dial-numerals');
  const hourHand = document.getElementById('hour-hand');
  const hourHandLeftFacet = document.getElementById('hour-hand-left-facet');
  const hourHandRightFacet = document.getElementById('hour-hand-right-facet');
  const hourHandRidge = document.getElementById('hour-hand-ridge');
  const minuteHand = document.getElementById('minute-hand');
  const minuteHandLeftFacet = document.getElementById('minute-hand-left-facet');
  const minuteHandRightFacet = document.getElementById('minute-hand-right-facet');
  const minuteHandRidge = document.getElementById('minute-hand-ridge');
  const secondHand = document.getElementById('second-hand');
  const secondGroup = document.getElementById('second-hand-group');
  if (!faceSvg || !handsSvg || !ticksGroup || !numeralsGroup || !hourHand || !hourHandLeftFacet || !hourHandRightFacet || !hourHandRidge || !minuteHand || !minuteHandLeftFacet || !minuteHandRightFacet || !minuteHandRidge || !secondHand || !secondGroup) return;

  ensureDialDefs(handsSvg);

  ticksGroup.replaceChildren();
  numeralsGroup.replaceChildren();

  const tickOuterRadius = 348;
  const minorLength = 16;
  const majorLength = 24;
  const quarterLength = 26;

  for (let i = 0; i < 60; i += 1) {
    const angle = i * 6;
    let length = minorLength;
    let tickClass = 'dial-tick minor';

    if (i % 5 === 0) {
      length = majorLength;
      tickClass = 'dial-tick major';
    }

    if (i % 15 === 0) {
      length = quarterLength;
      tickClass = 'dial-tick quarter';
    }

    const outer = polarPoint(angle, tickOuterRadius);
    const inner = polarPoint(angle, tickOuterRadius - length);
    const tick = createSvgElement('line', {
      class: tickClass,
      x1: outer.x,
      y1: outer.y,
      x2: inner.x,
      y2: inner.y
    });

    ticksGroup.appendChild(tick);
  }

  const numeralRadius = 286;
  const opticalOffsets = {
    12: { x: 0, y: -2 },
    3: { x: 1, y: 0 },
    6: { x: 0, y: 2 },
    9: { x: -1, y: 0 }
  };

  for (let hour = 0; hour < 12; hour += 1) {
    const displayHour = hour === 0 ? 12 : hour;
    const angle = hour * 30;
    const base = polarPoint(angle, numeralRadius);
    const offset = opticalOffsets[displayHour] || { x: 0, y: 0 };
    const numeral = createSvgElement('text', {
      class: `dial-numeral ${displayHour % 3 === 0 ? 'major' : 'minor'}`,
      x: base.x + offset.x,
      y: base.y + offset.y
    });

    numeral.textContent = String(displayHour);
    numeralsGroup.appendChild(numeral);
  }

  hourHand.setAttribute('d', 'M 380 238 L 398 266 L 392 350 L 387 406 L 380 424 L 373 406 L 368 350 L 362 266 Z');
  hourHandLeftFacet.setAttribute('d', 'M 380 238 L 362 266 L 368 350 L 373 406 L 380 424 L 380 405 L 377.6 350 L 373.4 270 Z');
  hourHandRightFacet.setAttribute('d', 'M 380 238 L 398 266 L 392 350 L 387 406 L 380 424 L 380 405 L 382.4 350 L 386.6 270 Z');
  hourHandRidge.setAttribute('d', 'M 380 248 L 386.2 269 L 384.2 350 L 381.9 404 L 380 415 L 378.1 404 L 375.8 350 L 373.8 269 Z');
  minuteHand.setAttribute('d', 'M 380 146 L 388.5 172 L 385.5 292 L 383.5 410 L 380 428 L 376.5 410 L 374.5 292 L 371.5 172 Z');
  minuteHandLeftFacet.setAttribute('d', 'M 380 146 L 371.5 172 L 374.5 292 L 376.5 410 L 380 428 L 380 408 L 378.8 292 L 376.8 176 Z');
  minuteHandRightFacet.setAttribute('d', 'M 380 146 L 388.5 172 L 385.5 292 L 383.5 410 L 380 428 L 380 408 L 381.2 292 L 383.2 176 Z');
  minuteHandRidge.setAttribute('d', 'M 380 154 L 383.6 179 L 382.6 292 L 381.3 407 L 380 420 L 378.7 407 L 377.4 292 L 376.4 179 Z');
  secondHand.setAttribute('d', 'M 380 120 L 382 144 L 381 382 L 379 382 L 378 144 Z M 377.5 386 L 382.5 386 L 381 446 L 379 446 Z');
  secondGroup.style.display = SECOND_HAND_ENABLED ? '' : 'none';
}

function updateHands() {
  const now = new Date();
  const hours = now.getHours() % 12;
  const minutes = now.getMinutes();
  const seconds = now.getSeconds();

  const hourAngle = (hours + minutes / 60) * 30;
  const minuteAngle = minutes * 6;
  const secondAngle = seconds * 6;

  const hourHand = document.getElementById('hour-hand-group');
  const minuteHand = document.getElementById('minute-hand-group');
  const secondHand = document.getElementById('second-hand-group');

  if (hourHand) hourHand.setAttribute('transform', `rotate(${hourAngle} 380 380)`);
  if (minuteHand) minuteHand.setAttribute('transform', `rotate(${minuteAngle} 380 380)`);
  if (secondHand && SECOND_HAND_ENABLED) secondHand.setAttribute('transform', `rotate(${secondAngle} 380 380)`);
}

function updateDayLabel() {
  const now = new Date();
  const day = now.toLocaleDateString('en-US', { weekday: 'long' });
  const dateShort = now.toLocaleDateString('en-US', { month: 'short', day: 'numeric' });
  const dayEl = document.getElementById('day');
  const dateEl = document.getElementById('date-short');

  if (dayEl) dayEl.textContent = day;
  if (dateEl) dateEl.textContent = dateShort;
}

function startClock() {
  buildDialSvg();
  updateDayLabel();
  updateHands();

  setInterval(updateHands, 1000);
  setInterval(updateDayLabel, 60 * 60 * 1000);
}

startClock();
