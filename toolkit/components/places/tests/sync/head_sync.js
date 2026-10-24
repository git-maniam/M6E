/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// Import common head.
{
  /* import-globals-from ../head_common.js */
  let commonFile = do_get_file("../head_common.js", false);
  let uri = Services.io.newFileURI(commonFile);
  Services.scriptloader.loadSubScriptWithOptions(uri.spec, {
    target: this,
    allowUnsafeURL: true,
  });
}

// Put any other stuff relative to this test folder below.

var { PlacesSyncUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/PlacesSyncUtils.sys.mjs"
);

function run_test() {
  do_get_profile();
  run_next_test();
}
