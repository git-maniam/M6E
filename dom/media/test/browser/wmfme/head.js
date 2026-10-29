"use strict";

/**
 * Return a web-based URL for a given file based on the testing directory.
 *
 * @param {string} fileName
 *        file that caller wants its web-based url
 * @param {boolean} cors [optional]
 *        if set, then return a url with different origin
 */
function GetTestWebBasedURL(fileName) {
  const origin = "https://example.com";
  return (
    getRootDirectory(gTestPath).replace("chrome://mochitests/content", origin) +
    fileName
  );
}

/**
 * Return current process Id for the Media Foundation CDM process.
 */
async function getMFCDMProcessId() {
  const process = (await ChromeUtils.requestProcInfo()).children.find(
    p =>
      p.type === "utility" &&
      p.utilityActors.find(a => a.actorName === "mfMediaEngineCDM")
  );
  return process.pid;
}

/**
 * Make video in the tab play.
 *
 * @param {object} tab
 *        the tab contains at least one video element
 */
async function playVideo(tab) {
  return SpecialPowers.spawn(tab.linkedBrowser, [], async _ => {
    const video = content.document.querySelector("video");
    ok(
      await video.play().then(
        () => true,
        () => false
      ),
      "video started playing"
    );
  });
}

/**
 * Check whether the video playback is performed in the right process and right decoder.
 *
 * @param {object} tab
 *        the tab which has a playing video
 * @param {string} expectedProcess
 *        the expected process name
 * @param {string} expectedDecoder
 *        the expected decoder name
 */
async function assertRunningProcessAndDecoderName(
  tab,
  { expectedProcess, expectedDecoder } = {}
) {
  return SpecialPowers.spawn(
    tab.linkedBrowser,
    [expectedProcess, expectedDecoder],
    // eslint-disable-next-line no-shadow
    async (expectedProcess, expectedDecoder) => {
      const video = content.document.querySelector("video");
      ok(!video.paused, "checking a playing video");

      const debugInfo = await SpecialPowers.wrap(video).mozRequestDebugInfo();
      const videoDecoderName = debugInfo.decoder.reader.videoDecoderName;

      const isExpectedDecoder =
        videoDecoderName.indexOf(`${expectedDecoder}`) == 0;
      ok(
        isExpectedDecoder,
        `Playback running by decoder '${videoDecoderName}', expected '${expectedDecoder}'`
      );

      const isExpectedProcess =
        videoDecoderName.indexOf(`(${expectedProcess} remote)`) > 0;
      ok(
        isExpectedProcess,
        `Playback running in process '${videoDecoderName}', expected '${expectedProcess}'`
      );
    }
  );
}

/**
 * Check whether the video playback is not performed in the given process and given decoder.
 *
 * @param {object} tab
 *        the tab which has a playing video
 * @param {string} givenProcess
 *        the process name on which the video playback should not be running
 * @param {string} givenDecoder
 *        the decoder name with which the video playback should not be running
 */
async function assertNotEqualRunningProcessAndDecoderName(
  tab,
  { givenProcess, givenDecoder } = {}
) {
  return SpecialPowers.spawn(
    tab.linkedBrowser,
    [givenProcess, givenDecoder],
    // eslint-disable-next-line no-shadow
    async (givenProcess, givenDecoder) => {
      const video = content.document.querySelector("video");
      ok(!video.paused, "checking a playing video");

      const debugInfo = await SpecialPowers.wrap(video).mozRequestDebugInfo();
      const videoDecoderName = debugInfo.decoder.reader.videoDecoderName;
      const pattern = /(.+?)\s+\((\S+)\s+remote\)/;
      const match = videoDecoderName.match(pattern);
      if (match) {
        const decoder = match[1];
        const process = match[2];
        isnot(decoder, givenDecoder, `Decoder name is not equal`);
        isnot(process, givenProcess, `Process name is not equal`);
      } else {
        ok(false, "failed to match decoder/process name?");
      }
    }
  );
}
