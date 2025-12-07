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
    | ScrollToEntry String
    | ScrollResult (Result Dom.Error ())
    | InputFocused
    | InputBlurred
    | InputKeyDown Int
    | OnScroll ScrollEvent
    | GotElementPosition { id : String, top : Float }
    | StreamDelta String
    | StreamDone
    | StreamError String
