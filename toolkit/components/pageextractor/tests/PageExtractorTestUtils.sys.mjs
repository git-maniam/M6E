/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * @import { PageExtractorParent } from "../PageExtractorParent.sys.mjs"
 */

import { BrowserTestUtils } from "resource://testing-common/BrowserTestUtils.sys.mjs";
import { HttpServer } from "resource://testing-common/httpd.sys.mjs";

/**
 * @param {TemplateStringsArray} strings
 * @param {any[]} values
 * @returns {string}
 */
function joinTemplate(strings, values) {
  let result = "";
  for (let i = 0; i < strings.length; i++) {
    result += strings[i];
    if (i < values.length) {
      result += values[i];
    }
  }
  return result;
}

/**
 * Start a server and return the URL for one of its registered paths.
 *
 * @param {HttpServer} server
 * @param {string} path
 * @returns {string}
 */
function startServer(server, path) {
  server.start(-1);
  const { primaryHost, primaryPort } = server.identity;
  // eslint-disable-next-line sdl/no-insecure-url
  return `http://${primaryHost}:${primaryPort}${path}`;
}

/**
 * Create an HTTP server that serves HTML once.
 *
 * @param {string} markup - The HTML content to serve
 * @param {number} code - HTTP status code
 * @returns {{url: string, cleanup: () => Promise<void>}}
 */
function createServer(markup, code) {
  const server = new HttpServer();

  const { promise, resolve } = Promise.withResolvers();
  const encoder = new TextEncoder();
  const htmlUtf8 = encoder.encode(markup);

  /** @type {nsIHttpRequestHandler} */
  const pageHandler = (request, response) => {
    response.setHeader("Content-Type", "text/html; charset=utf-8");
    response.setStatusLine(request.httpVersion, code, "");

    const binaryOutputStream = Cc[
      "@mozilla.org/binaryoutputstream;1"
    ].createInstance(Ci.nsIBinaryOutputStream);

    binaryOutputStream.setOutputStream(response.bodyOutputStream);
    binaryOutputStream.writeByteArray(
      /**
       * @type {any} - The type expects a number[], while we are passing a Uint8Array.
       */ (htmlUtf8)
    );

    resolve(server.stop());
  };

  server.registerPathHandler("/page.html", pageHandler);

  server.start(-1);

  let { primaryHost, primaryPort } = server.identity;
  // eslint-disable-next-line sdl/no-insecure-url
  const url = `http://${primaryHost}:${primaryPort}/page.html`;

  return {
    url,
    cleanup: () => promise,
  };
}

/**
 * Utilities for serving test pages to the page extractor tests.
 */
export const PageExtractorTestUtils = {
  /**
   * Serve HTML content via HTTP server but do not load it in a tab.
   *
   * The server serves the content once and then stops. The html`` tagged
   * template literal can be used as a way to provide syntax highlighting and
   * linting support within your editor.
   *
   * Example usage:
   *   const { html } = PageExtractorTestUtils.serveHTML({ code: 404 });
   *   const { url, cleanup } = html`<h1>Test</h1>`;
   *   // Use the URL...
   *   await cleanup();
   *
   * @param {object} options - Server configuration
   * @param {number} [options.code] - HTTP status code (default: 200)
   * @returns {{html: Function}}
   */
  serveHTML(options = {}) {
    const { code = 200 } = options;

    /**
     * Define the HTML and spin up a server.
     *
     * @param {TemplateStringsArray} strings - The literal string parts
     * @param {...any} values - The interpolated expressions
     * @returns {{url: string, cleanup: () => Promise<void>}}
     */
    function html(strings, ...values) {
      const markup = `<!DOCTYPE html><body>${joinTemplate(strings, values)}</body>`;
      return createServer(markup, code);
    }

    return { html };
  },

  /**
   * Serve a URL that accepts the request and never answers it, so the load
   * never commits.
   *
   * @returns {{url: string, cleanup: () => Promise<void>}}
   */
  serveStalledPage() {
    const server = new HttpServer();

    /** @type {any} */
    let heldResponse;

    server.registerPathHandler("/stalled.html", (_request, response) => {
      response.processAsync();
      response.setHeader("Content-Type", "text/html; charset=utf-8");
      heldResponse = response;
    });

    return {
      url: startServer(server, "/stalled.html"),
      async cleanup() {
        // Release the held request so the server can stop.
        heldResponse?.finish();
        await new Promise(resolve => server.stop(resolve));
      },
    };
  },

  /**
   * Similar to serveHTML, but loads the HTML automatically in a tab.
   *
   * Example usage:
   *   const { html } = PageExtractorTestUtils.serveHTMLInTab({ browser: gBrowser, code: 404 });
   *   const { tab, getPageExtractor, cleanup } = await html`
   *     <h1>Page Not Found</h1>
   *   `;
   *   // Use the tab
   *   await cleanup();
   *
   * @param {object} options - Server configuration
   * @param {object} options.browser - The gBrowser object from test scope
   * @param {number} [options.code] - HTTP status code (default: 200)
   */
  serveHTMLInTab(options) {
    const { browser, code = 200 } = options;

    if (!browser) {
      throw new Error(
        "browser is required. Pass it via serveHTMLInTab({ browser: gBrowser })"
      );
    }

    /**
     * Use a tagged template literal to create an HTML test page. This spins
     * up an HTTP server that serves the markup in a new tab.
     *
     * @param {TemplateStringsArray} strings - The literal string parts
     * @param {...any} values - The interpolated expressions
     */
    async function html(strings, ...values) {
      const markup = `<!DOCTYPE html><body>${joinTemplate(strings, values)}</body>`;
      const { url, cleanup: serverCleanup } = createServer(markup, code);

      const tab = await BrowserTestUtils.openNewForegroundTab(
        browser,
        url,
        true // waitForLoad
      );

      /**
       * Get a new page extractor, which can change when navigating pages.
       *
       * @returns {PageExtractorParent}
       */
      function getPageExtractor() {
        return tab.linkedBrowser.browsingContext.currentWindowGlobal.getActor(
          "PageExtractor"
        );
      }

      return {
        tab,
        url,
        getPageExtractor,
        async cleanup() {
          await serverCleanup();
          BrowserTestUtils.removeTab(tab);
        },
      };
    }

    return { html };
  },
};
