module Pages.Chat.View exposing (Actions, view)

{-| View for the Chat page.
-}

import Html.Styled as HS exposing (Html)
import Html.Styled.Attributes as HA
import Html.Styled.Events as HE
import Json.Decode as Decode
import Markdown.ChatMarkBlock as ChatMarkBlock exposing (ChatMarkBlock(..), StreamState, renderBlocksWithCopy, renderBlocksWithIdsAndCopy)
import Pages.Chat.Model exposing (ChatMessage, Model, TocEntry, scrollEventDecoder)
import Pages.Chat.Msg exposing (Msg(..))
import Settings exposing (SubmitShortcut(..))


{-| Actions provided by the parent for the view.
-}
type alias Actions msg =
    { toMsg : Msg -> msg
    , isConnected : Bool
    , onReconnect : msg
    , onGoHome : msg
    , onOpenSettings : msg
    , submitShortcut : SubmitShortcut
    }


{-| Render the Chat page.
-}
view : Actions msg -> Model -> Html msg
view actions model =
    HS.div
        [ HA.class "main-layout"
        , if model.sidebarVisible then
            HA.class ""
          else
            HA.class "sidebar-hidden"
        ]
        [ if model.sidebarVisible then
            viewSidebar actions model
          else
            HS.text ""
        , HS.div
            [ HA.class "content-column" ]
            [ HS.div
                [ HA.class "messages-wrapper" ]
                [ viewMessages actions model
                , HS.div [ HA.class "messages-fade" ] []
                ]
            , viewInput actions model
            ]
        ]


viewSidebar : Actions msg -> Model -> Html msg
viewSidebar actions model =
    let
        allTocEntries =
            model.tocEntriesHistory ++ model.tocEntriesStreaming

        toEntryContext prevWasUser entry =
            { prevWasUserQuery = prevWasUser
            , entry = entry
            , isActive = model.activeTocEntryId == Just entry.id
            }

        entriesWithContext =
            List.map2 toEntryContext
                (False :: List.map .isUserQuery allTocEntries)
                allTocEntries
    in
    HS.nav
        [ HA.class "toc-sidebar" ]
        [ viewConnectionStatus actions
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
                (List.map (viewTocEntryWithContext actions) entriesWithContext)
        ]


viewConnectionStatus : Actions msg -> Html msg
viewConnectionStatus actions =
    let
        ( statusText, statusClass ) =
            if actions.isConnected then
                ( "Connected", "status-connected" )

            else
                ( "Disconnected", "status-disconnected" )
    in
    HS.div
        [ HA.class "connection-status" ]
        [ HS.button
            [ HA.class "home-button"
            , HE.onClick actions.onGoHome
            , HA.title "Back to Home"
            ]
            [ HS.text "\u{2302}" ]  -- Unicode house symbol
        , HS.button
            [ HA.class "settings-button-sidebar"
            , HE.onClick actions.onOpenSettings
            , HA.title "Settings"
            ]
            [ HS.text "\u{2699}" ]  -- Unicode gear symbol
        , HS.div
            [ HA.class "status-indicator"
            , HA.class statusClass
            ]
            []
        , HS.text statusText
        , if not actions.isConnected then
            HS.button
                [ HA.class "reconnect-button"
                , HE.onClick actions.onReconnect
                ]
                [ HS.text "Reconnect" ]

          else
            HS.text ""
        ]


type alias TocEntryContext =
    { prevWasUserQuery : Bool
    , entry : TocEntry
    , isActive : Bool
    }


viewTocEntryWithContext : Actions msg -> TocEntryContext -> Html msg
viewTocEntryWithContext actions context =
    let
        entry =
            context.entry

        entryClass =
            if entry.isUserQuery then
                "toc-entry-user"

            else
                "toc-level-" ++ String.fromInt entry.level

        adjacentClass =
            if entry.isUserQuery && context.prevWasUserQuery then
                [ HA.class "toc-entry-user-adjacent" ]

            else
                []

        activeClass =
            if context.isActive then
                [ HA.class "toc-entry-active" ]

            else
                []
    in
    HS.li
        ([ HA.class "toc-entry"
         , HA.class entryClass
         ]
            ++ adjacentClass
            ++ activeClass
        )
        [ HS.button
            [ HA.class "toc-link"
            , HE.onClick (actions.toMsg (ScrollToEntry entry.id))
            ]
            [ HS.text entry.text ]
        ]


viewMessages : Actions msg -> Model -> Html msg
viewMessages actions model =
    let
        pendingText =
            ChatMarkBlock.getPending model.streamState

        streamingMessage =
            if model.isWaitingForResponse then
                [ viewStreamingMessage actions (List.length model.messages) model.streamState pendingText ]

            else
                []

        allContent =
            List.indexedMap (viewMessageWithIndex actions) model.messages ++ streamingMessage

        spacer =
            HS.div [ HA.class "messages-spacer" ] []
    in
    HS.div
        [ HA.class "messages-container"
        , HA.id "messages-container"
        , HE.on "scroll" (scrollEventDecoder |> Decode.map (OnScroll >> actions.toMsg))
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


viewStreamingMessage : Actions msg -> Int -> StreamState -> String -> Html msg
viewStreamingMessage actions msgIndex streamState pendingText =
    let
        completedBlocks =
            streamState.completedBlocks

        idPrefix =
            "msg-" ++ String.fromInt msgIndex

        onCopy =
            CopyToClipboard >> actions.toMsg
    in
    HS.div
        [ HA.class "message"
        , HA.class "message-assistant"
        ]
        [ HS.div
            [ HA.class "message-content" ]
            (renderBlocksWithIdsAndCopy onCopy idPrefix completedBlocks pendingText)
        ]


viewMessageWithIndex : Actions msg -> Int -> ChatMessage -> Html msg
viewMessageWithIndex actions msgIndex message =
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

        -- Use copy-enabled renderers for code blocks
        onCopy =
            CopyToClipboard >> actions.toMsg

        renderedContent =
            if message.role == "assistant" then
                renderBlocksWithIdsAndCopy onCopy idPrefix message.blocks ""

            else
                renderBlocksWithCopy onCopy message.blocks ""

        -- Extract raw text from all blocks for copying
        rawText =
            extractRawText message.blocks

        copyButton =
            if message.role == "user" || message.role == "assistant" then
                HS.div
                    [ HA.class "message-header" ]
                    [ HS.button
                        [ HA.class "copy-button"
                        , HE.onClick (actions.toMsg (CopyToClipboard rawText))
                        , HA.title "Copy to clipboard"
                        ]
                        [ HS.text "\u{1F4CB}" ]  -- Clipboard emoji
                    ]

            else
                HS.text ""
    in
    HS.div
        [ HA.class "message"
        , HA.class roleClass
        , HA.id idPrefix
        ]
        [ copyButton
        , HS.div
            [ HA.class "message-content" ]
            renderedContent
        ]


{-| Extract raw text from a list of ChatMarkBlocks.
-}
extractRawText : List ChatMarkBlock -> String
extractRawText blocks =
    blocks
        |> List.map extractBlockText
        |> String.join "\n"


extractBlockText : ChatMarkBlock -> String
extractBlockText block =
    case block of
        CompleteBlock raw _ ->
            raw

        PendingBlock raw ->
            raw

        ErrorBlock raw _ ->
            raw


viewInput : Actions msg -> Model -> Html msg
viewInput actions model =
    let
        isDisabled =
            not actions.isConnected || model.isWaitingForResponse

        wrapperStateClass =
            if isDisabled then
                "input-wrapper-inactive"

            else if model.inputFocused then
                "input-wrapper-focused"

            else
                "input-wrapper-ready"

        canSend =
            not isDisabled && String.trim model.userInput /= ""

        placeholderText =
            case actions.submitShortcut of
                EnterOnce ->
                    "Type your message... (press Enter to send, Esc to cancel)"

                ShiftEnter ->
                    "Type your message... (press Shift+Enter to send, Esc to cancel)"

                EnterTwice ->
                    "Type your message... (press Enter twice to send, Esc to cancel)"
    in
    HS.div
        [ HA.class "input-container" ]
        [ HS.div
            [ HA.class "input-wrapper"
            , HA.class wrapperStateClass
            ]
            [ HS.textarea
                [ HA.id "chat-input"
                , HA.class "input-textarea"
                , HA.value model.userInput
                , HE.onInput (UserInputChanged >> actions.toMsg)
                , HE.onFocus (actions.toMsg InputFocused)
                , HE.onBlur (actions.toMsg InputBlurred)
                , HE.preventDefaultOn "keydown" (enterKeyDecoder actions model.lastEnterTime model.isWaitingForResponse)
                , HA.placeholder placeholderText
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
                    , HE.onClick (actions.toMsg SendMessage)
                    , HA.disabled (not canSend)
                    , HA.title "Send message"
                    ]
                    [ HS.text "→" ]
                ]
            ]
        ]


enterKeyDecoder : Actions msg -> Int -> Bool -> Decode.Decoder ( msg, Bool )
enterKeyDecoder actions lastEnterTime isWaiting =
    let
        decodeKey key shiftKey altKey =
            if key == "Escape" && isWaiting then
                -- Handle Escape to cancel streaming
                Decode.succeed ( actions.toMsg CancelStream, True )

            else
                case actions.submitShortcut of
                    EnterOnce ->
                        -- Enter submits, Shift+Enter or Alt+Enter inserts newline
                        if key == "Enter" then
                            if shiftKey || altKey then
                                -- Let newline be inserted (don't prevent default)
                                Decode.fail "Insert newline"

                            else
                                -- Submit
                                Decode.succeed ( actions.toMsg SendMessage, True )

                        else
                            Decode.fail "Not a handled key"

                    ShiftEnter ->
                        -- Shift+Enter submits, Enter or Alt+Enter inserts newline
                        if key == "Enter" then
                            if shiftKey then
                                -- Submit
                                Decode.succeed ( actions.toMsg SendMessage, True )

                            else
                                -- Let newline be inserted (don't prevent default)
                                Decode.fail "Insert newline"

                        else
                            Decode.fail "Not a handled key"

                    EnterTwice ->
                        -- Double Enter submits, any Enter/Shift+Enter/Alt+Enter inserts newline
                        -- but we track timing to detect double-tap
                        if key == "Enter" then
                            Decode.field "timeStamp" Decode.float
                                |> Decode.map (toMsgWithPreventDefault shiftKey altKey)

                        else
                            Decode.fail "Not a handled key"

        toMsgWithPreventDefault shiftKey altKey timestamp =
            let
                currentTime =
                    round timestamp

                timeDiff =
                    currentTime - lastEnterTime

                isDoubleEnter =
                    timeDiff < 400 && timeDiff > 0
            in
            -- In EnterTwice mode: prevent default only on double-tap submit
            -- Single Enter should insert newline
            ( actions.toMsg (InputKeyDown currentTime), isDoubleEnter )
    in
    Decode.map3 decodeKey
        (Decode.field "key" Decode.string)
        (Decode.field "shiftKey" Decode.bool)
        (Decode.field "altKey" Decode.bool)
        |> Decode.andThen identity
