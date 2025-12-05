module Main exposing (main)

import Browser
import Browser.Dom as Dom
import Css.Global
import Html
import Html.Styled as HS exposing (Html)
import Html.Styled.Attributes as HA
import Html.Styled.Events as HE
import Json.Decode as Decode exposing (Value)
import Main.Debug
import Main.Style
import Markdown.ChatMarkBlock as ChatMarkBlock exposing (ChatMarkBlock, StreamState)
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
    , tocEntriesHistory : List TocEntry -- From finalized messages (cached)
    , tocEntriesStreaming : List TocEntry -- From current streaming response
    , inputFocused : Bool
    , lastEnterTime : Int
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
    | InputFocused
    | InputBlurred
    | InputKeyDown Int



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
            , streamState = ChatMarkBlock.initStreamState
            , isWaitingForResponse = False
            , tocEntriesHistory = []
            , tocEntriesStreaming = []
            , inputFocused = False
            , lastEnterTime = 0
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
                                { role = "user", blocks = [ ChatMarkBlock.PendingBlock model.userInput ] }

                            newMessageIndex =
                                List.length model.messages

                            newMessageId =
                                "msg-" ++ String.fromInt newMessageIndex
                        in
                        ( { model
                            | userInput = ""
                            , messages = model.messages ++ [ newMessage ]
                            , isWaitingForResponse = True
                            , streamState = ChatMarkBlock.initStreamState
                          }
                        , Cmd.batch
                            [ wsApi.send socketId queryJson WsSent
                            , scrollToEntry newMessageId
                            ]
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

        InputFocused ->
            ( { model | inputFocused = True }, Cmd.none )

        InputBlurred ->
            ( { model | inputFocused = False }, Cmd.none )

        InputKeyDown currentTime ->
            -- Double-Enter detection: if Enter pressed twice within 400ms, send message
            let
                timeDiff =
                    currentTime - model.lastEnterTime
            in
            if timeDiff < 400 && timeDiff > 0 then
                -- Double-enter detected, trigger send
                update SendMessage { model | lastEnterTime = 0 }

            else
                ( { model | lastEnterTime = currentTime }, Cmd.none )


{-| Scroll to a heading in the messages container.

To position the heading at the top of the scrollable container, we need to
calculate where the element is within the container's scrollable content:

    targetScrollPosition =
        elementDocY - containerDocY + currentScrollTop - topPadding

Where:

  - elementDocY: element's Y position in the document
  - containerDocY: container's Y position in the document
  - currentScrollTop: how much the container is already scrolled (viewport.y)
  - topPadding: small offset so element sits nicely below the top edge

This gives the absolute position of the element within the container's content,
which is independent of the current scroll position.

-}
scrollToEntry : String -> Cmd Msg
scrollToEntry targetId =
    Dom.getElement targetId
        |> Task.andThen
            (\element ->
                Dom.getElement "messages-container"
                    |> Task.andThen
                        (\container ->
                            Dom.getViewportOf "messages-container"
                                |> Task.andThen
                                    (\viewport ->
                                        let
                                            -- Element's position within container's content
                                            -- Subtract 8px so it sits nicely below the top edge
                                            targetY =
                                                element.element.y
                                                    - container.element.y
                                                    + viewport.viewport.y
                                                    - 8
                                        in
                                        Dom.setViewportOf "messages-container" 0 (max 0 targetY)
                                    )
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
                |> String.replace "\u{000D}" "\\r"
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
                    let
                        newStreamState =
                            ChatMarkBlock.feedDelta content model.streamState

                        -- Build TOC entries incrementally from streaming blocks
                        streamingMessageIndex =
                            List.length model.messages

                        newStreamingTocEntries =
                            generateTocEntriesForBlocks streamingMessageIndex newStreamState.completedBlocks
                    in
                    ( { model
                        | streamState = newStreamState
                        , tocEntriesStreaming = newStreamingTocEntries
                      }
                    , Cmd.none
                    )

                DoneMessage ->
                    let
                        finalBlocks =
                            ChatMarkBlock.finishStream model.streamState

                        assistantMessage =
                            { role = "assistant", blocks = finalBlocks }

                        newMessages =
                            model.messages ++ [ assistantMessage ]

                        -- Generate final TOC entries for this message
                        messageIndex =
                            List.length model.messages

                        finalTocEntries =
                            generateTocEntriesForBlocks messageIndex finalBlocks

                        -- Merge into history, clear streaming
                        newTocEntriesHistory =
                            model.tocEntriesHistory ++ finalTocEntries
                    in
                    ( { model
                        | messages = newMessages
                        , streamState = ChatMarkBlock.initStreamState
                        , isWaitingForResponse = False
                        , tocEntriesHistory = newTocEntriesHistory
                        , tocEntriesStreaming = []
                      }
                    , Cmd.none
                    )

                ErrorMessage errorMsg ->
                    let
                        errorChatMessage =
                            { role = "error", blocks = [ ChatMarkBlock.ErrorBlock "" errorMsg ] }
                    in
                    ( { model
                        | messages = model.messages ++ [ errorChatMessage ]
                        , streamState = ChatMarkBlock.initStreamState
                        , isWaitingForResponse = False
                        , tocEntriesStreaming = []
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


{-| Generate TOC entries from a list of blocks at a given message index.
Used for both streaming blocks and finalized message blocks.
-}
generateTocEntriesForBlocks : Int -> List ChatMarkBlock -> List TocEntry
generateTocEntriesForBlocks messageIndex blocks =
    let
        headings =
            ChatMarkBlock.extractHeadings blocks

        idPrefix =
            "msg-" ++ String.fromInt messageIndex
    in
    headings
        |> List.indexedMap
            (\headingIdx heading ->
                { id = idPrefix ++ "-heading-" ++ String.fromInt headingIdx
                , level = heading.level
                , text = heading.text
                , messageIndex = messageIndex
                }
            )



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
        [ -- Main.Debug.global |> Css.Global.global
          Main.Style.style |> Css.Global.global
        , HS.div
            [ HA.class "main-layout" ]
            [ viewSidebar model
            , HS.div
                [ HA.class "content-column" ]
                [ HS.div
                    [ HA.class "messages-wrapper" ]
                    [ viewMessages model
                    , HS.div [ HA.class "messages-fade" ] []
                    ]
                , viewInput model
                ]
            ]
        ]


{-| Render the left sidebar with connection status and table of contents.
-}
viewSidebar : Model -> Html Msg
viewSidebar model =
    let
        allTocEntries =
            model.tocEntriesHistory ++ model.tocEntriesStreaming
    in
    HS.nav
        [ HA.class "toc-sidebar" ]
        [ viewConnectionStatus model.connectionStatus
        , HS.h2
            [ HA.class "toc-title" ]
            [ HS.text "Contents" ]
        , if List.isEmpty allTocEntries then
            HS.p
                [ HA.class "toc-empty" ]
                [ HS.text "No headings yet" ]

          else
            HS.ul
                [ HA.class "toc-list" ]
                (List.map viewTocEntry allTocEntries)
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
            ChatMarkBlock.getPending model.streamState

        -- Add streaming message if there's activity
        streamingMessage =
            if model.isWaitingForResponse then
                [ viewStreamingMessage (List.length model.messages) model.streamState pendingText ]

            else
                []

        allContent =
            List.indexedMap viewMessageWithIndex model.messages ++ streamingMessage

        -- Dynamic spacer that fills remaining viewport space
        -- This allows the last message to be scrolled to the top
        spacer =
            HS.div [ HA.class "messages-spacer" ] []
    in
    HS.div
        [ HA.class "messages-container"
        , HA.id "messages-container"
        ]
        [ HS.div
            [ HA.class "messages-content" ]
            (if List.isEmpty model.messages && not model.isWaitingForResponse then
                [ HS.p
                    [ HA.class "messages-empty" ]
                    [ HS.text "No messages yet. Send a message to start chatting." ]
                ]

             else
                allContent ++ [ spacer ]
            )
        ]


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
            [ HA.class "message-content" ]
            (ChatMarkBlock.renderBlocksWithIds idPrefix completedBlocks pendingText)
        ]


{-| Render a message with its index for ID generation.
-}
viewMessageWithIndex : Int -> ChatMessage -> Html Msg
viewMessageWithIndex msgIndex message =
    let
        roleClass =
            case message.role of
                "user" ->
                    "message-user"

                "assistant" ->
                    "message-assistant"

                "error" ->
                    "message-error"

                _ ->
                    "message-assistant"

        idPrefix =
            "msg-" ++ String.fromInt msgIndex

        -- Use renderBlocksWithIds for assistant messages to enable TOC navigation
        renderedContent =
            if message.role == "assistant" then
                ChatMarkBlock.renderBlocksWithIds idPrefix message.blocks ""

            else
                ChatMarkBlock.renderBlocks message.blocks ""
    in
    HS.div
        [ HA.class "message"
        , HA.class roleClass
        , HA.id idPrefix
        ]
        [ HS.div
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

        -- Determine wrapper state class for border colors
        wrapperStateClass =
            if isDisabled then
                "input-wrapper-inactive"

            else if model.inputFocused then
                "input-wrapper-focused"

            else
                "input-wrapper-ready"

        canSend =
            not isDisabled && String.trim model.userInput /= ""
    in
    HS.div
        [ HA.class "input-container" ]
        [ HS.div
            [ HA.class "input-wrapper"
            , HA.class wrapperStateClass
            ]
            [ HS.textarea
                [ HA.class "input-textarea"
                , HA.value model.userInput
                , HE.onInput UserInputChanged
                , HE.onFocus InputFocused
                , HE.onBlur InputBlurred
                , HE.preventDefaultOn "keydown" (enterKeyDecoder model.lastEnterTime)
                , HA.placeholder "Type your message... (press Enter twice to send)"
                , HA.disabled isDisabled
                ]
                []
            , HS.div
                [ HA.class "input-toolbar" ]
                [ HS.button
                    [ HA.class "input-send-button"
                    , if canSend then
                        HA.class "input-send-button-enabled"

                      else
                        HA.class "input-send-button-disabled"
                    , HE.onClick SendMessage
                    , HA.disabled (not canSend)
                    , HA.title "Send message"
                    ]
                    [ HS.text "→" ]
                ]
            ]
        ]


{-| Decoder for Enter key press that captures the timestamp.
Only triggers on Enter key, ignores other keys.
Returns (Msg, preventDefault) - we prevent default on double-enter to stop
the newline from being added after the input is cleared.
-}
enterKeyDecoder : Int -> Decode.Decoder ( Msg, Bool )
enterKeyDecoder lastEnterTime =
    Decode.field "key" Decode.string
        |> Decode.andThen
            (\key ->
                if key == "Enter" then
                    Decode.field "timeStamp" Decode.float
                        |> Decode.map
                            (\timestamp ->
                                let
                                    currentTime =
                                        round timestamp

                                    timeDiff =
                                        currentTime - lastEnterTime

                                    isDoubleEnter =
                                        timeDiff < 400 && timeDiff > 0
                                in
                                ( InputKeyDown currentTime, isDoubleEnter )
                            )

                else
                    Decode.fail "Not Enter key"
            )
