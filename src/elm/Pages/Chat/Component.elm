module Pages.Chat.Component exposing
    ( Model
    , Msg
    , Protocol
    , init
    , update
    , view
    , receiveStreamDelta
    , receiveStreamDone
    , receiveStreamError
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


type alias Protocol model msg =
    Update.Protocol model msg


{-| Initialize the Chat page model with an optional chat ID.
-}
init : Maybe String -> ( Model, Cmd msg )
init chatId =
    ( { userInput = ""
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


{-| Handle stream error from websocket.
-}
receiveStreamError : Protocol model msg -> String -> Model -> ( model, Cmd msg )
receiveStreamError =
    Update.receiveStreamError
