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
  cragUrl: computeCragUrl()
};

console.log("Connecting to CRAG backend at:", flags.cragUrl);

const app = Elm.Main.init({ node: appNode, flags: flags });

// Initialize WebSocket ports
new WebsocketPorts(app);

// Scroll tracking for TOC highlighting
function setupScrollListener(containerId: string) {
  // Use requestAnimationFrame to ensure DOM is ready
  requestAnimationFrame(() => {
    const container = document.getElementById(containerId);
    if (!container) {
      console.warn("Scroll container not found:", containerId);
      return;
    }

    let ticking = false;

    const sendScrollData = () => {
      // Find all elements with IDs that start with "msg-" (TOC targets)
      const elements = container.querySelectorAll('[id^="msg-"]');
      const containerRect = container.getBoundingClientRect();

      const elementPositions: { id: string; top: float }[] = [];
      elements.forEach((el) => {
        const rect = el.getBoundingClientRect();
        // Position relative to container's top
        const relativeTop = rect.top - containerRect.top + container.scrollTop;
        elementPositions.push({
          id: el.id,
          top: relativeTop
        });
      });

      app.ports.onScroll.send({
        scrollTop: container.scrollTop,
        containerHeight: container.clientHeight,
        elementPositions: elementPositions
      });
    };

    container.addEventListener("scroll", () => {
      if (!ticking) {
        requestAnimationFrame(() => {
          sendScrollData();
          ticking = false;
        });
        ticking = true;
      }
    });

    // Send initial scroll data
    sendScrollData();
  });
}

// Subscribe to setupScrollListener port
if (app.ports.setupScrollListener) {
  app.ports.setupScrollListener.subscribe(setupScrollListener);
}
