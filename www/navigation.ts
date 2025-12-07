// Navigation ports for URL routing

interface ElmApp {
  ports: {
    pushUrl?: {
      subscribe: (callback: (url: string) => void) => void;
    };
    onUrlChange?: {
      send: (href: string) => void;
    };
  };
}

export class NavigationPorts {
  private app: ElmApp;

  constructor(app: ElmApp) {
    this.app = app;

    // Listen for browser back/forward navigation
    window.addEventListener("popstate", this.handlePopState.bind(this));

    // Subscribe to URL change requests from Elm
    if (app.ports.pushUrl) {
      app.ports.pushUrl.subscribe(this.handlePushUrl.bind(this));
    }

    // Send initial URL to Elm
    this.sendCurrentUrl();
  }

  private handlePopState(_event: PopStateEvent): void {
    this.sendCurrentUrl();
  }

  private handlePushUrl(url: string): void {
    history.pushState({}, "", url);
    this.sendCurrentUrl();
  }

  private sendCurrentUrl(): void {
    if (this.app.ports.onUrlChange) {
      this.app.ports.onUrlChange.send(location.href);
    }
  }
}
