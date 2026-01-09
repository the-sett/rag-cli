module Pages.Chat.Msg exposing (Msg(..))

{-| Messages for the Chat page.
-}

import Browser.Dom as Dom
import Pages.Chat.Model exposing (ScrollEvent)


{-| Chat page messages.
-}
type Msg
    = UserInputChanged String
    | SendMessage
    | CancelStream
    | CopyToClipboard String
    | ScrollToEntry String
    | ScrollResult (Result Dom.Error ())
    | InputFocused
    | InputBlurred
    | InputKeyDown Int
    | OnScroll ScrollEvent
    | GotElementPosition { id : String, top : Float }
    | RefreshTocPositions
    | StreamDelta String
    | StreamDone (Maybe String)  -- Optional chat ID from server
    | StreamCancelled
    | StreamError String
