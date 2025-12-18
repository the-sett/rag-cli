module Pages.Agents.View exposing (Actions, view)

{-| View for the Agents page.
-}

import Html.Styled as HS exposing (Html)
import Html.Styled.Attributes as HA
import Html.Styled.Events as HE
import Json.Decode as Decode
import Pages.Agents.Model exposing (AgentInfo, Model)
import Pages.Agents.Msg exposing (Msg(..))
import Settings exposing (SubmitShortcut(..))


{-| Actions provided by the parent for the view.
-}
type alias Actions msg =
    { toMsg : Msg -> msg
    , isConnected : Bool
    , onGoHome : msg
    , onOpenSettings : msg
    , submitShortcut : SubmitShortcut
    }


{-| Render the Agents page.
-}
view : Actions msg -> Model -> Html msg
view actions model =
    HS.div
        [ HA.class "main-layout" ]
        [ viewSidebar actions model
        , HS.div
            [ HA.class "content-column" ]
            [ viewAgentEditor actions model
            ]
        ]


viewSidebar : Actions msg -> Model -> Html msg
viewSidebar actions model =
    HS.nav
        [ HA.class "toc-sidebar" ]
        [ viewConnectionStatus actions
        , HS.h2
            [ HA.class "toc-title" ]
            [ HS.text "Agents" ]
        , HS.button
            [ HA.class "new-agent-button"
            , HE.onClick (actions.toMsg NewAgent)
            ]
            [ HS.text "+ New Agent" ]
        , if model.loading then
            HS.p
                [ HA.class "toc-empty" ]
                [ HS.text "Loading..." ]

          else if List.isEmpty model.agents then
            HS.p
                [ HA.class "toc-empty" ]
                [ HS.text "No agents yet" ]

          else
            HS.ul
                [ HA.class "toc-list" ]
                (List.map (viewAgentItem actions model.selectedAgentId) model.agents)
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
            [ HS.text "\u{2302}" ]
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
        ]


viewAgentItem : Actions msg -> Maybe String -> AgentInfo -> Html msg
viewAgentItem actions selectedAgentId agent =
    let
        isSelected =
            selectedAgentId == Just agent.id

        selectedClass =
            if isSelected then
                [ HA.class "toc-entry-active" ]

            else
                []
    in
    HS.li
        ([ HA.class "toc-entry"
         , HA.class "toc-entry-user"
         ]
            ++ selectedClass
        )
        [ HS.button
            [ HA.class "toc-link"
            , HE.onClick (actions.toMsg (SelectAgent agent.id))
            ]
            [ HS.text agent.name ]
        ]


viewAgentEditor : Actions msg -> Model -> Html msg
viewAgentEditor actions model =
    let
        isDisabled =
            model.loading

        wrapperStateClass =
            if isDisabled then
                "input-wrapper-inactive"

            else if model.inputFocused then
                "input-wrapper-focused"

            else
                "input-wrapper-ready"

        canSave =
            not isDisabled && String.trim model.userInput /= ""

        headerText =
            case model.selectedAgentId of
                Just _ ->
                    "Edit Agent"

                Nothing ->
                    "New Agent"

        placeholderText =
            case actions.submitShortcut of
                EnterOnce ->
                    "Enter agent instructions... (first line becomes the name, press Enter to save)"

                ShiftEnter ->
                    "Enter agent instructions... (first line becomes the name, press Shift+Enter to save)"

                EnterTwice ->
                    "Enter agent instructions... (first line becomes the name, press Enter twice to save)"
    in
    HS.div
        [ HA.class "agent-editor" ]
        [ HS.h2
            [ HA.class "agent-editor-title" ]
            [ HS.text headerText ]
        , case model.error of
            Just errorMsg ->
                HS.div
                    [ HA.class "agent-error" ]
                    [ HS.text errorMsg ]

            Nothing ->
                HS.text ""
        , HS.div
            [ HA.class "input-container" ]
            [ HS.div
                [ HA.class "input-wrapper"
                , HA.class wrapperStateClass
                ]
                [ HS.textarea
                    [ HA.class "input-textarea"
                    , HA.value model.userInput
                    , HE.onInput (UserInputChanged >> actions.toMsg)
                    , HE.onFocus (actions.toMsg InputFocused)
                    , HE.onBlur (actions.toMsg InputBlurred)
                    , HE.preventDefaultOn "keydown" (enterKeyDecoder actions model.lastEnterTime)
                    , HA.placeholder placeholderText
                    , HA.disabled isDisabled
                    ]
                    []
                , HS.div
                    [ HA.class "input-toolbar" ]
                    [ HS.button
                        [ HA.class "input-send-button"
                        , if canSave then
                            HA.class "input-send-button-enabled"

                          else
                            HA.class "input-send-button-disabled"
                        , HE.onClick (actions.toMsg SubmitAgent)
                        , HA.disabled (not canSave)
                        , HA.title "Save agent"
                        ]
                        [ HS.text "Save" ]
                    ]
                ]
            ]
        ]


enterKeyDecoder : Actions msg -> Int -> Decode.Decoder ( msg, Bool )
enterKeyDecoder actions lastEnterTime =
    let
        decodeKey key shiftKey altKey =
            case actions.submitShortcut of
                EnterOnce ->
                    -- Enter submits, Shift+Enter or Alt+Enter inserts newline
                    if key == "Enter" then
                        if shiftKey || altKey then
                            -- Let newline be inserted (don't prevent default)
                            Decode.fail "Insert newline"

                        else
                            -- Submit
                            Decode.succeed ( actions.toMsg SubmitAgent, True )

                    else
                        Decode.fail "Not a handled key"

                ShiftEnter ->
                    -- Shift+Enter submits, Enter or Alt+Enter inserts newline
                    if key == "Enter" then
                        if shiftKey then
                            -- Submit
                            Decode.succeed ( actions.toMsg SubmitAgent, True )

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
                            |> Decode.map toMsgWithPreventDefault

                    else
                        Decode.fail "Not a handled key"

        toMsgWithPreventDefault timestamp =
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
