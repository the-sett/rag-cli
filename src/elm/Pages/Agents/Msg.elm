module Pages.Agents.Msg exposing (Msg(..))

{-| Messages for the Agents page.
-}

import Http
import Pages.Agents.Model exposing (AgentInfo)


{-| Agents page messages.
-}
type Msg
    = UserInputChanged String
    | SubmitAgent
    | SelectAgent String
    | NewAgent
    | InputFocused
    | InputBlurred
    | InputKeyDown Int
    | GotAgents (Result Http.Error (List AgentInfo))
    | AgentSaved (Result Http.Error AgentInfo)
