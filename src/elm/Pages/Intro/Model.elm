module Pages.Intro.Model exposing (Model, ChatInfo)

{-| Model for the Intro page.
-}


{-| Information about a chat session.
-}
type alias ChatInfo =
    { id : String
    , title : String
    , createdAt : String
    }


{-| The Intro page model.
Stores chat list and loading state.
-}
type alias Model =
    { chats : List ChatInfo
    , loading : Bool
    , error : Maybe String
    }
