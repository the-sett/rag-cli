/**
 * WebSocket implementation following the elm-procedure pattern.
 * Manages WebSocket connections with request-response ID correlation.
 */

export function checkPortsExist(app: any, portNames: string[]) {
    if (!app.ports) {
        throw new Error("The Elm application has no ports.");
    }

    const allPorts = `[${Object.keys(app.ports).sort().join(", ")}]`;

    for (const portName of portNames) {
        if (!Object.prototype.hasOwnProperty.call(app.ports, portName)) {
            throw new Error(
                `Could not find a port named ${portName} among: ${allPorts}`
            );
        }
    }
}

type SocketState = {
    socket: WebSocket;
    url: string;
};

type OpenArgs = {
    id: string;
    url: string;
};

type SendArgs = {
    id: string;
    socketId: string;
    payload: string;
};

type CloseArgs = {
    id: string;
    socketId: string;
};

type Ports = {
    wsOpen: { subscribe: (callback: (args: OpenArgs) => void) => void };
    wsSend: { subscribe: (callback: (args: SendArgs) => void) => void };
    wsClose: { subscribe: (callback: (args: CloseArgs) => void) => void };
    wsResponse: {
        send: (response: {
            id: string;
            type_: string;
            socketId: string;
            response: any;
        }) => void;
    };
    wsOnMessage: {
        send: (message: { socketId: string; payload: string }) => void;
    };
    wsOnClose: { send: (socketId: string) => void };
    wsOnError: {
        send: (error: { socketId: string; error: string }) => void;
    };
};

export class WebsocketPorts {
    app: { ports: Ports };
    sockets: Map<string, SocketState> = new Map();

    constructor(app: any) {
        this.app = app;

        checkPortsExist(app, [
            "wsOpen",
            "wsSend",
            "wsClose",
            "wsResponse",
            "wsOnMessage",
            "wsOnClose",
            "wsOnError",
        ]);

        app.ports.wsOpen.subscribe(this.open);
        app.ports.wsSend.subscribe(this.send);
        app.ports.wsClose.subscribe(this.close);
    }

    open = (args: OpenArgs) => {
        try {
            const socket = new WebSocket(args.url);

            socket.onopen = () => {
                // Store the socket with the request ID as the socket ID
                this.sockets.set(args.id, {
                    socket: socket,
                    url: args.url,
                });

                // Send success response with the socket ID
                this.app.ports.wsResponse.send({
                    id: args.id,
                    type_: "Ok",
                    socketId: args.id,
                    response: null,
                });
            };

            socket.onmessage = (evt: MessageEvent) => {
                this.app.ports.wsOnMessage.send({
                    socketId: args.id,
                    payload: evt.data,
                });
            };

            socket.onclose = () => {
                this.sockets.delete(args.id);
                this.app.ports.wsOnClose.send(args.id);
            };

            socket.onerror = (evt: Event) => {
                // If we haven't successfully opened yet, send error response
                if (!this.sockets.has(args.id)) {
                    this.app.ports.wsResponse.send({
                        id: args.id,
                        type_: "Error",
                        socketId: args.id,
                        response: "WebSocket connection failed",
                    });
                } else {
                    // Otherwise send async error
                    this.app.ports.wsOnError.send({
                        socketId: args.id,
                        error: "WebSocket error",
                    });
                }
            };
        } catch (e) {
            this.app.ports.wsResponse.send({
                id: args.id,
                type_: "Error",
                socketId: args.id,
                response: e instanceof Error ? e.message : "Unknown error",
            });
        }
    };

    send = (args: SendArgs) => {
        const state = this.sockets.get(args.socketId);

        if (state) {
            try {
                state.socket.send(args.payload);

                this.app.ports.wsResponse.send({
                    id: args.id,
                    type_: "Ok",
                    socketId: args.socketId,
                    response: null,
                });
            } catch (e) {
                this.app.ports.wsResponse.send({
                    id: args.id,
                    type_: "Error",
                    socketId: args.socketId,
                    response: e instanceof Error ? e.message : "Send failed",
                });
            }
        } else {
            this.app.ports.wsResponse.send({
                id: args.id,
                type_: "Error",
                socketId: args.socketId,
                response: "Socket not found",
            });
        }
    };

    close = (args: CloseArgs) => {
        const state = this.sockets.get(args.socketId);

        if (state) {
            state.socket.close();
            this.sockets.delete(args.socketId);

            this.app.ports.wsResponse.send({
                id: args.id,
                type_: "Ok",
                socketId: args.socketId,
                response: null,
            });
        } else {
            this.app.ports.wsResponse.send({
                id: args.id,
                type_: "Error",
                socketId: args.socketId,
                response: "Socket not found",
            });
        }
    };
}
