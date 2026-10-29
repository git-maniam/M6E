/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

let defaultValue;
add_task(async function default_need() {
  defaultValue = await ShellService.doesAppNeedPin();

  Assert.notStrictEqual(
    defaultValue,
    undefined,
    "Got a default app need pin value"
  );
});

add_task(async function restore_default() {
  if (defaultValue === undefined) {
    info("No default pin value set, so nothing to test");
    return;
  }

  Assert.equal(
    await ShellService.doesAppNeedPin(),
    defaultValue,
    "Pinning restored to original"
  );
});
