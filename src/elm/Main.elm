module Main exposing (main)

import Browser
import Browser.Dom as Dom
import Css.Global
import Html
import Html.Styled as HS exposing (Html)
import Html.Styled.Attributes as HA
import Html.Styled.Events as HE
import Json.Decode as Decode exposing (Value)
import Main.Style
import Markdown.Render as MdRender exposing (ChatMarkBlock, StreamState)
import Ports
import Procedure.Program
import Task
import Websocket



-- Flags


type alias Flags =
    { cragUrl : String
    }


flagsDecoder : Decode.Decoder Flags
flagsDecoder =
    Decode.map Flags
        (Decode.field "cragUrl" Decode.string)



-- Main


main : Program Value Model Msg
main =
    Browser.element
        { init = init
        , update = update
        , view = view
        , subscriptions = subscriptions
        }



-- Model


type ConnectionStatus
    = Disconnected
    | Connecting
    | Connected Websocket.SocketId


type alias Model =
    { procedureState : Procedure.Program.Model Msg
    , cragUrl : String
    , connectionStatus : ConnectionStatus
    , userInput : String
    , messages : List ChatMessage
    , streamState : StreamState
    , isWaitingForResponse : Bool
    , tocEntries : List TocEntry
    }


type alias ChatMessage =
    { role : String
    , blocks : List ChatMarkBlock
    }


{-| Table of contents entry derived from markdown headings.
-}
type alias TocEntry =
    { id : String -- Unique DOM id for scrolling target
    , level : Int -- Heading level (1-6)
    , text : String -- Heading text content
    , messageIndex : Int -- Which message this belongs to
    }


type Msg
    = ProcedureMsg (Procedure.Program.Msg Msg)
    | WsOpened (Result Websocket.Error Websocket.SocketId)
    | WsSent (Result Websocket.Error ())
    | WsClosed (Result Websocket.Error ())
    | WsMessage Websocket.SocketId String
    | WsClosedAsync Websocket.SocketId
    | WsError Websocket.SocketId String
    | UserInputChanged String
    | SendMessage
    | Reconnect
    | ScrollToEntry String
    | ScrollResult (Result Dom.Error ())



-- WebSocket API helper


wsApi : Websocket.WebsocketApi Msg
wsApi =
    Websocket.websocketApi ProcedureMsg Ports.websocketPorts



-- Init


init : Value -> ( Model, Cmd Msg )
init flagsValue =
    let
        flags =
            Decode.decodeValue flagsDecoder flagsValue
                |> Result.withDefault { cragUrl = "ws://localhost:8193" }

        model =
            { procedureState = Procedure.Program.init
            , cragUrl = flags.cragUrl
            , connectionStatus = Connecting
            , userInput = ""
            , messages = []
            , streamState = MdRender.initStreamState
            , isWaitingForResponse = False
            , tocEntries = []
            }
    in
    ( model
    , wsApi.open flags.cragUrl WsOpened
    )



-- Update


update : Msg -> Model -> ( Model, Cmd Msg )
update msg model =
    case msg of
        ProcedureMsg procMsg ->
            Procedure.Program.update procMsg model.procedureState
                |> Tuple.mapFirst (\ps -> { model | procedureState = ps })

        WsOpened result ->
            case result of
                Ok socketId ->
                    ( { model | connectionStatus = Connected socketId }
                    , Cmd.none
                    )

                Err _ ->
                    ( { model | connectionStatus = Disconnected }
                    , Cmd.none
                    )

        WsSent _ ->
            ( model, Cmd.none )

        WsClosed _ ->
            ( { model | connectionStatus = Disconnected }
            , Cmd.none
            )

        WsMessage _ payload ->
            handleServerMessage payload model

        WsClosedAsync _ ->
            ( { model | connectionStatus = Disconnected }
            , Cmd.none
            )

        WsError _ _ ->
            ( model, Cmd.none )

        UserInputChanged input ->
            ( { model | userInput = input }, Cmd.none )

        SendMessage ->
            case model.connectionStatus of
                Connected socketId ->
                    if String.trim model.userInput /= "" then
                        let
                            queryJson =
                                "{\"type\":\"query\",\"content\":" ++ encodeString model.userInput ++ "}"

                            newMessage =
                                { role = "user", blocks = [ MdRender.PendingBlock model.userInput ] }
                        in
                        ( { model
                            | userInput = ""
                            , messages = model.messages ++ [ newMessage ]
                            , isWaitingForResponse = True
                            , streamState = MdRender.initStreamState
                          }
                        , wsApi.send socketId queryJson WsSent
                        )

                    else
                        ( model, Cmd.none )

                _ ->
                    ( model, Cmd.none )

        Reconnect ->
            ( { model | connectionStatus = Connecting }
            , wsApi.open model.cragUrl WsOpened
            )

        ScrollToEntry targetId ->
            ( model, scrollToEntry targetId )

        ScrollResult _ ->
            -- Ignore scroll result (success or failure)
            ( model, Cmd.none )


{-| Scroll to a heading in the messages container.
-}
scrollToEntry : String -> Cmd Msg
scrollToEntry targetId =
    Dom.getElement targetId
        |> Task.andThen
            (\element ->
                Dom.getViewportOf "messages-container"
                    |> Task.andThen
                        (\viewport ->
                            let
                                -- Calculate the target scroll position
                                -- element.element.y is relative to the document
                                -- We need to account for the current scroll position
                                targetY =
                                    element.element.y
                                        - viewport.viewport.y
                                        + viewport.viewport.y
                                        - 20
                                -- 20px padding from top
                            in
                            Dom.setViewportOf "messages-container" 0 (max 0 targetY)
                        )
            )
        |> Task.attempt ScrollResult


{-| Simple JSON string encoder
-}
encodeString : String -> String
encodeString str =
    "\""
        ++ (str
                |> String.replace "\\" "\\\\"
                |> String.replace "\"" "\\\""
                |> String.replace "\n" "\\n"
                |> String.replace "\r" "\\r"
                |> String.replace "\t" "\\t"
           )
        ++ "\""


{-| Handle incoming server messages
-}
handleServerMessage : String -> Model -> ( Model, Cmd Msg )
handleServerMessage payload model =
    case Decode.decodeString serverMessageDecoder payload of
        Ok serverMsg ->
            case serverMsg of
                DeltaMessage content ->
                    ( { model | streamState = MdRender.feedDelta content model.streamState }
                    , Cmd.none
                    )

                DoneMessage ->
                    let
                        finalBlocks =
                            MdRender.finishStream model.streamState

                        assistantMessage =
                            { role = "assistant", blocks = finalBlocks }

                        newMessages =
                            model.messages ++ [ assistantMessage ]

                        newTocEntries =
                            generateTocEntries newMessages
                    in
                    ( { model
                        | messages = newMessages
                        , streamState = MdRender.initStreamState
                        , isWaitingForResponse = False
                        , tocEntries = newTocEntries
                      }
                    , Cmd.none
                    )

                ErrorMessage errorMsg ->
                    let
                        errorChatMessage =
                            { role = "error", blocks = [ MdRender.ErrorBlock "" errorMsg ] }
                    in
                    ( { model
                        | messages = model.messages ++ [ errorChatMessage ]
                        , streamState = MdRender.initStreamState
                        , isWaitingForResponse = False
                      }
                    , Cmd.none
                    )

        Err _ ->
            ( model, Cmd.none )


type ServerMessage
    = DeltaMessage String
    | DoneMessage
    | ErrorMessage String


serverMessageDecoder : Decode.Decoder ServerMessage
serverMessageDecoder =
    Decode.field "type" Decode.string
        |> Decode.andThen
            (\msgType ->
                case msgType of
                    "delta" ->
                        Decode.map DeltaMessage (Decode.field "content" Decode.string)

                    "done" ->
                        Decode.succeed DoneMessage

                    "error" ->
                        Decode.map ErrorMessage (Decode.field "message" Decode.string)

                    _ ->
                        Decode.fail ("Unknown message type: " ++ msgType)
            )


{-| Generate TOC entries from all messages.
Only extracts headings from assistant messages.
-}
generateTocEntries : List ChatMessage -> List TocEntry
generateTocEntries messages =
    messages
        |> List.indexedMap
            (\msgIdx message ->
                if message.role == "assistant" then
                    let
                        headings =
                            MdRender.extractHeadings message.blocks

                        idPrefix =
                            "msg-" ++ String.fromInt msgIdx
                    in
                    headings
                        |> List.indexedMap
                            (\headingIdx heading ->
                                { id = idPrefix ++ "-heading-" ++ String.fromInt headingIdx
                                , level = heading.level
                                , text = heading.text
                                , messageIndex = msgIdx
                                }
                            )

                else
                    []
            )
        |> List.concat



-- Subscriptions


subscriptions : Model -> Sub Msg
subscriptions model =
    Sub.batch
        [ Procedure.Program.subscriptions model.procedureState
        , wsApi.onMessage WsMessage
        , wsApi.onClose WsClosedAsync
        , wsApi.onError WsError
        ]



-- View


view : Model -> Html.Html Msg
view model =
    viewStyled model |> HS.toUnstyled


viewStyled : Model -> Html Msg
viewStyled model =
    HS.div
        [ HA.class "app-container" ]
        [ Main.Style.style |> Css.Global.global
        , HS.div
            [ HA.class "main-layout" ]
            [ viewTableOfContents model.tocEntries
            , HS.div
                [ HA.class "content-column" ]
                [ viewHeader model
                , viewMessages model
                , viewInput model
                ]
            ]
        ]


viewHeader : Model -> Html Msg
viewHeader model =
    HS.div
        [ HA.class "header" ]
        [ HS.h1
            [ HA.class "header-title" ]
            [ HS.text "CRAG Web Interface" ]
        , viewConnectionStatus model.connectionStatus
        ]


viewConnectionStatus : ConnectionStatus -> Html Msg
viewConnectionStatus status =
    let
        ( statusText, statusClass ) =
            case status of
                Disconnected ->
                    ( "Disconnected", "status-disconnected" )

                Connecting ->
                    ( "Connecting...", "status-connecting" )

                Connected _ ->
                    ( "Connected", "status-connected" )
    in
    HS.div
        [ HA.class "connection-status" ]
        [ HS.div
            [ HA.class "status-indicator"
            , HA.class statusClass
            ]
            []
        , HS.text statusText
        , case status of
            Disconnected ->
                HS.button
                    [ HA.class "reconnect-button"
                    , HE.onClick Reconnect
                    ]
                    [ HS.text "Reconnect" ]

            _ ->
                HS.text ""
        ]


{-| Render the table of contents sidebar.
-}
viewTableOfContents : List TocEntry -> Html Msg
viewTableOfContents entries =
    HS.nav
        [ HA.class "toc-sidebar" ]
        [ HS.h2
            [ HA.class "toc-title" ]
            [ HS.text "Contents" ]
        , if List.isEmpty entries then
            HS.p
                [ HA.class "toc-empty" ]
                [ HS.text "No headings yet" ]

          else
            HS.ul
                [ HA.class "toc-list" ]
                (List.map viewTocEntry entries)
        ]


{-| Render a single TOC entry.
-}
viewTocEntry : TocEntry -> Html Msg
viewTocEntry entry =
    HS.li
        [ HA.class "toc-entry"
        , HA.class ("toc-level-" ++ String.fromInt entry.level)
        ]
        [ HS.button
            [ HA.class "toc-link"
            , HE.onClick (ScrollToEntry entry.id)
            ]
            [ HS.text entry.text ]
        ]


viewMessages : Model -> Html Msg
viewMessages model =
    let
        -- Get pending text from stream state for in-progress rendering
        pendingText =
            MdRender.getPending model.streamState

        -- Add streaming message if there's activity
        streamingMessage =
            if model.isWaitingForResponse then
                [ viewStreamingMessage (List.length model.messages) model.streamState pendingText ]

            else
                []

        allContent =
            List.indexedMap viewMessageWithIndex model.messages ++ streamingMessage
    in
    HS.div
        [ HA.class "messages-container"
        , HA.id "messages-container"
        ]
        (if List.isEmpty model.messages && not model.isWaitingForResponse then
            [ HS.p
                [ HA.class "messages-empty" ]
                [ HS.text "No messages yet. Send a message to start chatting." ]
            ]

         else
            allContent
        )


viewStreamingMessage : Int -> StreamState -> String -> Html Msg
viewStreamingMessage msgIndex streamState pendingText =
    let
        completedBlocks =
            streamState.completedBlocks

        idPrefix =
            "msg-" ++ String.fromInt msgIndex
    in
    HS.div
        [ HA.class "message"
        , HA.class "message-assistant"
        ]
        [ HS.div
            [ HA.class "message-label" ]
            [ HS.text "Assistant" ]
        , HS.div
            [ HA.class "message-content" ]
            (MdRender.renderBlocksWithIds idPrefix completedBlocks pendingText)
        ]


{-| Render a message with its index for ID generation.
-}
viewMessageWithIndex : Int -> ChatMessage -> Html Msg
viewMessageWithIndex msgIndex message =
    let
        ( roleClass, label ) =
            case message.role of
                "user" ->
                    ( "message-user", "You" )

                "assistant" ->
                    ( "message-assistant", "Assistant" )

                "error" ->
                    ( "message-error", "Error" )

                _ ->
                    ( "message-assistant", message.role )

        idPrefix =
            "msg-" ++ String.fromInt msgIndex

        -- Use renderBlocksWithIds for assistant messages to enable TOC navigation
        renderedContent =
            if message.role == "assistant" then
                MdRender.renderBlocksWithIds idPrefix message.blocks ""

            else
                MdRender.renderBlocks message.blocks ""
    in
    HS.div
        [ HA.class "message"
        , HA.class roleClass
        ]
        [ HS.div
            [ HA.class "message-label" ]
            [ HS.text label ]
        , HS.div
            [ HA.class "message-content" ]
            renderedContent
        ]


viewInput : Model -> Html Msg
viewInput model =
    let
        isDisabled =
            case model.connectionStatus of
                Connected _ ->
                    model.isWaitingForResponse

                _ ->
                    True

        buttonClass =
            if isDisabled || String.trim model.userInput == "" then
                "send-button-disabled"

            else
                "send-button-enabled"
    in
    HS.div
        [ HA.class "input-container" ]
        [ HS.textarea
            [ HA.class "input-textarea"
            , HA.value model.userInput
            , HE.onInput UserInputChanged
            , HA.placeholder "Type your message..."
            , HA.rows 3
            , HA.disabled isDisabled
            ]
            []
        , HS.button
            [ HA.class "send-button"
            , HA.class buttonClass
            , HE.onClick SendMessage
            , HA.disabled (isDisabled || String.trim model.userInput == "")
            ]
            [ HS.text
                (if model.isWaitingForResponse then
                    "..."

                 else
                    "Send"
                )
            ]
        ]
