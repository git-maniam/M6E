"use strict";

// An external script, so that the page makes a script request and parses it.
function pageTelemetryHelper(n) {
  let values = [];
  for (let i = 0; i < n; i++) {
    values.push(i * 2);
  }
  return values.reduce((a, b) => a + b, 0);
}
pageTelemetryHelper(1000);
