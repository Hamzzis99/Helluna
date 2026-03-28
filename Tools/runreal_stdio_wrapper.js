"use strict";

const os = require("os");
const path = require("path");
const dgram = require("dgram");
const util = require("util");

const runrealPackageRoot = "C:\\Users\\kgh\\AppData\\Local\\npm-cache\\_npx\\db42a14b5fa74470\\node_modules\\@runreal\\unreal-mcp";
const remoteExecutionModulePath = path.join(
  "C:\\Users\\kgh\\AppData\\Local\\npm-cache\\_npx\\db42a14b5fa74470\\node_modules\\unreal-remote-execution",
  "dist",
  "index.js"
);

function writeToStderr(args) {
  const message = args
    .map((value) =>
      typeof value === "string"
        ? value
        : util.inspect(value, { depth: 6, colors: false, breakLength: Infinity })
    )
    .join(" ");

  process.stderr.write(message + "\n");
}

console.log = (...args) => writeToStderr(args);
console.info = (...args) => writeToStderr(args);
console.warn = (...args) => writeToStderr(args);

function getUdpTargets() {
  const targets = new Set(["127.0.0.1"]);

  for (const interfaces of Object.values(os.networkInterfaces())) {
    for (const entry of interfaces || []) {
      if (entry && entry.family === "IPv4" && !entry.internal) {
        targets.add(entry.address);
      }
    }
  }

  return [...targets];
}

function sendDirectOpenConnection(nodeId, commandHost, commandPort) {
  const payload = Buffer.from(
    JSON.stringify({
      version: 1,
      magic: "ue_py",
      source: nodeId,
      type: "open_connection",
      data: {
        command_ip: commandHost,
        command_port: commandPort,
      },
    })
  );

  const socket = dgram.createSocket("udp4");
  const targets = getUdpTargets();
  let sentCount = 0;

  socket.on("error", (...args) => writeToStderr(args));

  const interval = setInterval(() => {
    for (const target of targets) {
      socket.send(payload, 6766, target);
    }

    sentCount += 1;
    if (sentCount >= 8) {
      clearInterval(interval);
      socket.close();
    }
  }, 250);
}

const remoteExecutionModule = require(remoteExecutionModulePath);
const originalStart = remoteExecutionModule.RemoteExecution.prototype.start;

remoteExecutionModule.RemoteExecution.prototype.start = async function patchedStart() {
  await originalStart.call(this);

  if (this.broadcastConnection) {
    this.broadcastConnection.broadcastOpenConnection = () => {
      const [commandHost, commandPort] = this.config.commandEndpoint;
      sendDirectOpenConnection(this.nodeId, commandHost, commandPort);
    };

    this.broadcastConnection.broadcastCloseConnection = () => {};
  }
};

remoteExecutionModule.RemoteExecution.prototype.getFirstRemoteNode = async function patchedGetFirstRemoteNode() {
  return {
    nodeId: "direct-open-connection",
    data: {},
    update() {},
    shouldTimeout() {
      return false;
    },
  };
};

require(path.join(runrealPackageRoot, "dist", "bin.js"));
