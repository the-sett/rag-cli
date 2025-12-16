module Pages.Intro.View exposing (Actions, view)

{-| View for the Intro page.
-}

import Html.Styled as HS exposing (Html)
import Html.Styled.Attributes as HA
import Html.Styled.Events as HE
import Json.Decode as Decode
import Pages.Intro.Model exposing (AgentInfo, ChatInfo, Model)
import Pages.Intro.Msg exposing (Msg(..))


{-| Actions provided by the parent for the view.
-}
type alias Actions msg =
    { toMsg : Msg -> msg
    }


{-| Render the Intro page.
-}
view : Actions msg -> Model -> Html msg
view actions model =
    HS.div
        [ HA.class "intro-page" ]
        [ HS.div
            [ HA.class "intro-header" ]
            [ HS.h1
                [ HA.class "intro-title" ]
                [ HS.text "CRAG" ]
            , HS.p
                [ HA.class "intro-subtitle" ]
                [ HS.text "RAG CLI Web Interface" ]
            , HS.div
                [ HA.class "intro-buttons" ]
                [ HS.button
                    [ HA.class "intro-ready-button"
                    , HE.onClick (actions.toMsg Ready)
                    ]
                    [ HS.text "New Chat" ]
                , HS.button
                    [ HA.class "intro-agents-button"
                    , HE.onClick (actions.toMsg GoToAgents)
                    ]
                    [ HS.text "Manage Agents" ]
                ]
            ]
        , HS.div
            [ HA.class "intro-columns" ]
            [ viewChatList actions model
            , viewAgentList actions model
            ]
        , viewDeleteConfirmModal actions model
        ]


{-| Render the chat list section.
-}
viewChatList : Actions msg -> Model -> Html msg
viewChatList actions model =
    HS.div
        [ HA.class "intro-column" ]
        [ HS.h2
            [ HA.class "intro-column-title" ]
            [ HS.text "Previous Chats" ]
        , if model.loadingChats then
            HS.div
                [ HA.class "intro-list-loading" ]
                [ HS.text "Loading chats..." ]

          else if List.isEmpty model.chats then
            HS.div
                [ HA.class "intro-list-empty" ]
                [ HS.text "No previous chats" ]

          else
            HS.ul
                [ HA.class "intro-list-items" ]
                (List.map (viewChatItem actions) model.chats)
        ]


{-| Render a single chat item.
-}
viewChatItem : Actions msg -> ChatInfo -> Html msg
viewChatItem actions chat =
    HS.li
        [ HA.class "intro-list-item"
        , HE.onClick (actions.toMsg (SelectChat chat.id))
        ]
        [ HS.div
            [ HA.class "intro-item-content" ]
            [ HS.div
                [ HA.class "intro-item-title" ]
                [ HS.text (displayTitle chat.title) ]
            , HS.div
                [ HA.class "intro-item-date" ]
                [ HS.text (formatDate chat.createdAt) ]
            ]
        , HS.button
            [ HA.class "intro-delete-button"
            , HE.stopPropagationOn "click"
                (Decode.succeed ( actions.toMsg (RequestDeleteChat chat.id), True ))
            , HA.title "Delete chat"
            ]
            [ HS.text "\u{1F5D1}" ]
        ]


{-| Render the agent list section.
-}
viewAgentList : Actions msg -> Model -> Html msg
viewAgentList actions model =
    HS.div
        [ HA.class "intro-column" ]
        [ HS.h2
            [ HA.class "intro-column-title" ]
            [ HS.text "New Agent Chat" ]
        , if model.loadingAgents then
            HS.div
                [ HA.class "intro-list-loading" ]
                [ HS.text "Loading agents..." ]

          else if List.isEmpty model.agents then
            HS.div
                [ HA.class "intro-list-empty" ]
                [ HS.text "No agents yet - create one using Manage Agents" ]

          else
            HS.ul
                [ HA.class "intro-list-items" ]
                (List.map (viewAgentItem actions) model.agents)
        ]


{-| Render a single agent item.
-}
viewAgentItem : Actions msg -> AgentInfo -> Html msg
viewAgentItem actions agent =
    HS.li
        [ HA.class "intro-list-item"
        , HE.onClick (actions.toMsg (SelectAgentChat agent.id))
        ]
        [ HS.div
            [ HA.class "intro-item-title" ]
            [ HS.text agent.name ]
        , HS.div
            [ HA.class "intro-item-date" ]
            [ HS.text (formatDate agent.createdAt) ]
        ]


{-| Display chat title or fallback for empty titles.
-}
displayTitle : String -> String
displayTitle title =
    if String.isEmpty title then
        "(Untitled)"

    else
        title


{-| Format a date string for display.
Takes ISO 8601 format and returns a more readable format.
-}
formatDate : String -> String
formatDate dateStr =
    -- Simple formatting: just show the date part
    -- Full date: "2024-12-07T14:30:22"
    String.left 10 dateStr


{-| Render the delete confirmation modal.
-}
viewDeleteConfirmModal : Actions msg -> Model -> Html msg
viewDeleteConfirmModal actions model =
    case model.deletingChatId of
        Nothing ->
            HS.text ""

        Just chatId ->
            let
                chatTitle =
                    model.chats
                        |> List.filter (\c -> c.id == chatId)
                        |> List.head
                        |> Maybe.map .title
                        |> Maybe.withDefault "(Untitled)"
                        |> displayTitle
            in
            HS.div
                [ HA.class "modal-overlay"
                , HE.onClick (actions.toMsg CancelDeleteChat)
                ]
                [ HS.div
                    [ HA.class "modal-content"
                    , HE.stopPropagationOn "click" (Decode.succeed ( actions.toMsg CancelDeleteChat, False ))
                    ]
                    [ HS.h3
                        [ HA.class "modal-title" ]
                        [ HS.text "Delete Chat" ]
                    , HS.p
                        [ HA.class "modal-message" ]
                        [ HS.text "Are you sure you want to delete \""
                        , HS.text chatTitle
                        , HS.text "\"?"
                        ]
                    , HS.div
                        [ HA.class "modal-buttons" ]
                        [ HS.button
                            [ HA.class "modal-cancel-button"
                            , HE.onClick (actions.toMsg CancelDeleteChat)
                            ]
                            [ HS.text "Cancel" ]
                        , HS.button
                            [ HA.class "modal-delete-button"
                            , HE.onClick (actions.toMsg ConfirmDeleteChat)
                            ]
                            [ HS.text "Delete" ]
                        ]
                    ]
                ]
