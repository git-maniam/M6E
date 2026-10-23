/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * M6E: the page telemetry actor (D29). The parent side only sends queries,
 * through M6EPageTelemetry.collect().
 */
export class M6EPageTelemetryParent extends JSWindowActorParent {}
