module Pages.Chat.Component exposing
    ( Model
    , Msg
    , cancelStreamMsg
    , Protocol
    , init
    , update
    , view
    , receiveStreamDelta
    , receiveStreamDone
    , receiveStreamCancelled
    , receiveStreamError
    , receiveHistoryMessage
    , scrollToBottom
    , setSidebarVisible
    )

{-| Chat page component.
-}

import Html.Styled exposing (Html)
import Markdown.ChatMarkBlock as ChatMarkBlock
import Pages.Chat.Model as Model
import Pages.Chat.Msg as Msg
import Pages.Chat.Update as Update
import Pages.Chat.View as View


type alias Model =
    Model.Model


type alias Msg =
    Msg.Msg


{-| Expose CancelStream message for external use.
-}
cancelStreamMsg : Msg
cancelStreamMsg =
    Msg.CancelStream


type alias Protocol model msg =
    Update.Protocol model msg


{-| Initialize the Chat page model with an optional chat ID.
-}
init : Maybe String -> ( Model, Cmd msg )
init chatId =
    ( { userInput = ""
      , pendingUserInput = Nothing
      , messages = []
      , streamState = ChatMarkBlock.initStreamState
      , isWaitingForResponse = False
      , tocEntriesHistory = []
      , tocEntriesStreaming = []
      , inputFocused = False
      , lastEnterTime = 0
      , activeTocEntryId = Nothing
      , tocElementPositions = []
      , chatId = chatId
      , sidebarVisible = True
      }
    , Cmd.none
    )


{-| Update the Chat page.
-}
update : Protocol model msg -> Msg -> Model -> ( model, Cmd msg )
update =
    Update.update


{-| Render the Chat page.
-}
view : View.Actions msg -> Model -> Html msg
view =
    View.view


{-| Handle incoming stream delta from websocket.
-}
receiveStreamDelta : Protocol model msg -> String -> Model -> ( model, Cmd msg )
receiveStreamDelta =
    Update.receiveStreamDelta


{-| Handle stream completion from websocket.
-}
receiveStreamDone : Protocol model msg -> Maybe String -> Model -> ( model, Cmd msg )
receiveStreamDone =
    Update.receiveStreamDone


{-| Handle stream cancellation from websocket.
-}
receiveStreamCancelled : Protocol model msg -> Model -> ( model, Cmd msg )
receiveStreamCancelled =
    Update.receiveStreamCancelled


{-| Handle stream error from websocket.
-}
receiveStreamError : Protocol model msg -> String -> Model -> ( model, Cmd msg )
receiveStreamError =
    Update.receiveStreamError


{-| Handle history message from websocket (for reconnecting to existing chats).
-}
receiveHistoryMessage : Protocol model msg -> String -> String -> Model -> ( model, Cmd msg )
receiveHistoryMessage =
    Update.receiveHistoryMessage


{-| Scroll the chat to the bottom. Used after loading history.
-}
scrollToBottom : (Msg -> msg) -> Cmd msg
scrollToBottom toMsg =
    Update.scrollToBottom |> Cmd.map toMsg


{-| Set sidebar visibility. Used by MCP tools.
-}
setSidebarVisible : Protocol model msg -> Bool -> Model -> ( model, Cmd msg )
setSidebarVisible protocol visible model =
    protocol.onUpdate ( { model | sidebarVisible = visible }, Cmd.none )
