/*
 * main.js
 *
 * Copyright (C) 2021 by RStudio, PBC
 *
 * Unless you have received this program directly from RStudio pursuant
 * to the terms of a commercial license agreement with RStudio, then
 * this program is licensed to you under the terms of version 3 of the
 * GNU Affero General Public License. This program is distributed WITHOUT
 * ANY EXPRESS OR IMPLIED WARRANTY, INCLUDING THOSE OF NON-INFRINGEMENT,
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Please refer to the
 * AGPL (http://www.gnu.org/licenses/agpl-3.0.txt) for more details.
 *
 */

const { app } = require('electron');
const os = require('os');
const fs = require('fs');
const path = require('path');
const SessionLauncher = require('./session-launcher');

module.exports = class Main {
  constructor() {
  }

  run() {
    // look for a version check request; if we have one, just do that and exit
    if (process.argv.includes('--version', 1)) {
      console.log('RSTUDIO_VERSION is 0.0.0.0.00001');
      app.exit(0);
      return;
  }

    // ignore SIGPIPE
    process.on('SIGPIPE', () => {});

    this.initializeSharedSecret();

    // get install path
    let installPath = '../../cpp/cmake-build-debug';
    if (!fs.existsSync(installPath)) {
      console.log(`Unable to find ${installPath}`);
      app.exit(1);
      return;
    }
    installPath = path.resolve(installPath);

    // calculate paths to config file, rsession, and desktop scripts
    let confPath = path.join(installPath, 'conf/rdesktop-dev.conf');
    let sessionPath = path.join(installPath, 'session/rsession');
    let scriptsPath = path.join(installPath, 'desktop');
    let devMode = true;

    if (!this.prepareEnvironment(scriptsPath)) {
      console.log(`Failed to prepare environment`);
      app.exit(1);
      return;
    }

    let launcher = new SessionLauncher(sessionPath, confPath);
    launcher.launchFirstSession(installPath, devMode);
  }

  initializeSharedSecret() {
    let secret = "12345";
    process.env.RS_SHARED_SECRET = secret;

  }

  prepareEnvironment(scriptsPath) {
    // attempt to detect R environment
    let ldScriptPath = path.join(scriptsPath, '../session/r-ldpath');

    // whole bunch of code...

    process.env.R_HOME = "/Library/Frameworks/R.framework/Resources";
    process.env.R_SHARE_DIR = "/Library/Frameworks/R.framework/Resources/share";
    process.env.R_INCLUDE_DIR = "/Library/Frameworks/R.framework/Resources/include";
    process.env.R_DOC_DIR = "/Library/Frameworks/R.framework/Resources/doc";
    process.env.DYLD_FALLBACK_LIBRARY_PATH = "/Library/Frameworks/R.framework/Resources/lib:/Users/gary/lib:/usr/local/lib:/usr/lib:::/lib:/Library/Java/JavaVirtualMachines/jdk-11.0.1.jdk/Contents/Home/lib/server";
    process.env.RS_CRASH_HANDLER_PATH = "/opt/rstudio-tools/crashpad/crashpad/out/Default/crashpad_handler";

    // uncomment to stall start of rsession for # seconds so you can attach debugger to it
    // process.env.RSTUDIO_SESSION_SLEEP_ON_STARTUP = "15";

    return true;
  }
}
