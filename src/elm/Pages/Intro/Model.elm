module Pages.Intro.Model exposing (Model, ChatInfo, AgentInfo)

{-| Model for the Intro page.
-}


{-| Information about a chat session.
-}
type alias ChatInfo =
    { id : String
    , title : String
    , createdAt : String
    }


{-| Information about an agent.
-}
type alias AgentInfo =
    { id : String
    , name : String
    , instructions : String
    , createdAt : String
    }


{-| The Intro page model.
Stores chat list, agent list, and loading state.
-}
type alias Model =
    { chats : List ChatInfo
    , agents : List AgentInfo
    , loadingChats : Bool
    , loadingAgents : Bool
    , error : Maybe String
    }
