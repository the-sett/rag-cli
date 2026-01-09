import { NavigationPorts } from "./navigation";
import { WebsocketPorts } from "./websockets";

// Type declarations for Elm application
interface ElmApp {
  ports: any;
}

interface ElmMain {
  init: (options: { node: HTMLElement | null; flags: any }) => ElmApp;
}

interface ElmNamespace {
  Main: ElmMain;
}

declare const Elm: ElmNamespace;

// Compute WebSocket URL from current location
// HTTP server is on port N, WebSocket server is on port N+1
function computeCragUrl(): string {
  const protocol = window.location.protocol === "https:" ? "wss:" : "ws:";
  const host = window.location.hostname;
  const httpPort = parseInt(window.location.port) || (window.location.protocol === "https:" ? 443 : 80);
  const wsPort = httpPort + 1;
  return `${protocol}//${host}:${wsPort}`;
}

// Initialize Elm application with flags
const appNode = document.getElementById("app");
const flags = {
  cragUrl: computeCragUrl(),
  locationHref: window.location.href
};

console.log("Connecting to CRAG backend at:", flags.cragUrl);

const app = Elm.Main.init({ node: appNode, flags: flags });

// Initialize ports
new NavigationPorts(app);
new WebsocketPorts(app);

// Debug logging port
if (app.ports.debugLog) {
  app.ports.debugLog.subscribe((msg: string) => {
    console.log("[Elm Debug]", msg);
  });
}

// Clipboard port
if (app.ports.copyToClipboard) {
  app.ports.copyToClipboard.subscribe((text: string) => {
    navigator.clipboard.writeText(text).catch((err) => {
      console.error("Failed to copy to clipboard:", err);
    });
  });
}
