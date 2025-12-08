module Pages.Agents.Model exposing
    ( Model
    , AgentInfo
    )

{-| Model for the Agents page.
-}


{-| The Agents page model.
-}
type alias Model =
    { userInput : String
    , agents : List AgentInfo
    , selectedAgentId : Maybe String
    , inputFocused : Bool
    , lastEnterTime : Int
    , loading : Bool
    , error : Maybe String
    }


{-| Agent information from the backend.
-}
type alias AgentInfo =
    { id : String
    , name : String
    , instructions : String
    , createdAt : String
    }
