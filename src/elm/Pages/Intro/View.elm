module Pages.Intro.View exposing (Actions, view)

{-| View for the Intro page.
-}

import Html.Styled as HS exposing (Html)
import Html.Styled.Attributes as HA
import Html.Styled.Events as HE
import Pages.Intro.Model exposing (Model, ChatInfo)
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
            [ HA.class "intro-content" ]
            [ HS.h1
                [ HA.class "intro-title" ]
                [ HS.text "CRAG" ]
            , HS.p
                [ HA.class "intro-subtitle" ]
                [ HS.text "RAG CLI Web Interface" ]
            , HS.button
                [ HA.class "intro-ready-button"
                , HE.onClick (actions.toMsg Ready)
                ]
                [ HS.text "New Chat" ]
            , viewChatList actions model
            ]
        ]


{-| Render the chat list section.
-}
viewChatList : Actions msg -> Model -> Html msg
viewChatList actions model =
    if model.loading then
        HS.div
            [ HA.class "chat-list-loading" ]
            [ HS.text "Loading chats..." ]

    else if List.isEmpty model.chats then
        HS.div
            [ HA.class "chat-list-empty" ]
            [ HS.text "No previous chats" ]

    else
        HS.div
            [ HA.class "chat-list" ]
            [ HS.h2
                [ HA.class "chat-list-title" ]
                [ HS.text "Previous Chats" ]
            , HS.ul
                [ HA.class "chat-list-items" ]
                (List.map (viewChatItem actions) model.chats)
            ]


{-| Render a single chat item.
-}
viewChatItem : Actions msg -> ChatInfo -> Html msg
viewChatItem actions chat =
    HS.li
        [ HA.class "chat-list-item"
        , HE.onClick (actions.toMsg (SelectChat chat.id))
        ]
        [ HS.div
            [ HA.class "chat-item-title" ]
            [ HS.text (displayTitle chat.title) ]
        , HS.div
            [ HA.class "chat-item-date" ]
            [ HS.text (formatDate chat.createdAt) ]
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
