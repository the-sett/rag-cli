module Pages.Intro.Msg exposing (Msg(..))

{-| Messages for the Intro page.
-}

import Http
import Pages.Intro.Model exposing (AgentInfo, ChatInfo)


{-| Intro page messages.
-}
type Msg
    = Ready
    | SelectChat String
    | SelectAgentChat String
    | GoToAgents
    | GotChats (Result Http.Error (List ChatInfo))
    | GotAgents (Result Http.Error (List AgentInfo))
    | FetchChats
    | FetchAgents
