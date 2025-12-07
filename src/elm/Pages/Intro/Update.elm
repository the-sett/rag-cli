module Pages.Intro.Update exposing (Protocol, update, fetchChats)

{-| Update logic for the Intro page.
-}

import Http
import Json.Decode as Decode exposing (Decoder)
import Pages.Intro.Model exposing (Model, ChatInfo)
import Pages.Intro.Msg exposing (Msg(..))


{-| Protocol for communicating with the parent.
-}
type alias Protocol model msg =
    { toMsg : Msg -> msg
    , onUpdate : ( Model, Cmd msg ) -> ( model, Cmd msg )
    , onReady : ( Model, Cmd msg ) -> ( model, Cmd msg )
    , onSelectChat : String -> ( Model, Cmd msg ) -> ( model, Cmd msg )
    }


{-| Update the Intro page model.
-}
update : Protocol model msg -> Msg -> Model -> ( model, Cmd msg )
update protocol msg model =
    case msg of
        Ready ->
            ( model, Cmd.none )
                |> protocol.onReady

        SelectChat chatId ->
            ( model, Cmd.none )
                |> protocol.onSelectChat chatId

        FetchChats ->
            ( { model | loading = True, error = Nothing }
            , fetchChats protocol.toMsg
            )
                |> protocol.onUpdate

        GotChats result ->
            case result of
                Ok chats ->
                    ( { model | chats = chats, loading = False }
                    , Cmd.none
                    )
                        |> protocol.onUpdate

                Err _ ->
                    ( { model | error = Just "Failed to load chats", loading = False }
                    , Cmd.none
                    )
                        |> protocol.onUpdate


{-| Fetch the chat list from the server.
-}
fetchChats : (Msg -> msg) -> Cmd msg
fetchChats toMsg =
    Http.get
        { url = "/api/chats"
        , expect = Http.expectJson (toMsg << GotChats) chatsDecoder
        }


{-| Decoder for a list of chat info.
-}
chatsDecoder : Decoder (List ChatInfo)
chatsDecoder =
    Decode.list chatInfoDecoder


{-| Decoder for a single chat info.
-}
chatInfoDecoder : Decoder ChatInfo
chatInfoDecoder =
    Decode.map3 ChatInfo
        (Decode.field "id" Decode.string)
        (Decode.field "title" Decode.string)
        (Decode.field "created_at" Decode.string)
