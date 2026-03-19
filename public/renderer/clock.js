function buildTickRing() {
  const ring = document.getElementById('tick-ring');
  if (!ring) return;

  const outerAnchorRadius = 348;
  const minorLength = 16;
  const majorLength = 24;
  const quarterLength = 26;

  for (let i = 0; i < 60; i += 1) {
    const tick = document.createElement('span');
    tick.className = 'tick';

    let tickLength = minorLength;

    if (i % 5 === 0) {
      tick.classList.add('major');
      tickLength = majorLength;
    }

    if (i % 15 === 0) {
      tick.classList.add('quarter');
      tickLength = quarterLength;
    }

    const radius = outerAnchorRadius - tickLength / 2;
    const angle = i * 6;
    tick.style.transform = `translate(-50%, -50%) rotate(${angle}deg) translateY(-${radius}px)`;

    ring.appendChild(tick);
  }
}

function buildHourRing() {
  const ring = document.getElementById('hour-ring');
  if (!ring) return;

  const radius = 286;

  for (let hour = 0; hour < 12; hour += 1) {
    const marker = document.createElement('span');
    const angle = hour * 30;
    const displayHour = hour === 0 ? 12 : hour;
    const isMajor = displayHour % 3 === 0;
    const opticalOffsets = {
      12: { x: 0, y: -2 },
      3: { x: 1, y: 0 },
      6: { x: 0, y: 2 },
      9: { x: -1, y: 0 }
    };
    const offset = opticalOffsets[displayHour] || { x: 0, y: 0 };

    marker.className = `marker ${isMajor ? 'major' : 'minor'}`;
    marker.textContent = String(displayHour);
    marker.style.transform = `translate(-50%, -50%) rotate(${angle}deg) translateY(-${radius}px) rotate(-${angle}deg) translate(${offset.x}px, ${offset.y}px)`;

    ring.appendChild(marker);
  }
}

function updateHands() {
  const now = new Date();
  const hours = now.getHours() % 12;
  const minutes = now.getMinutes();

  const hourAngle = (hours + minutes / 60) * 30;
  const minuteAngle = minutes * 6;

  const hourHand = document.getElementById('hour-hand');
  const minuteHand = document.getElementById('minute-hand');

  if (hourHand) hourHand.style.transform = `rotate(${hourAngle}deg)`;
  if (minuteHand) minuteHand.style.transform = `rotate(${minuteAngle}deg)`;
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
  buildTickRing();
  buildHourRing();
  updateDayLabel();
  updateHands();

  setInterval(updateHands, 1000);
  setInterval(updateDayLabel, 60 * 60 * 1000);
}

startClock();
