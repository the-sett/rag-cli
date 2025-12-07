module Pages.Intro.Msg exposing (Msg(..))

{-| Messages for the Intro page.
-}

import Http
import Pages.Intro.Model exposing (ChatInfo)


{-| Intro page messages.
-}
type Msg
    = Ready
    | SelectChat String
    | GotChats (Result Http.Error (List ChatInfo))
    | FetchChats
