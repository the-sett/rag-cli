module Pages.Intro.Update exposing (Protocol, update, fetchChats, fetchAgents)

{-| Update logic for the Intro page.
-}

import Http
import Json.Decode as Decode exposing (Decoder)
import Pages.Intro.Model exposing (AgentInfo, ChatInfo, Model)
import Pages.Intro.Msg exposing (Msg(..))


{-| Protocol for communicating with the parent.
-}
type alias Protocol model msg =
    { toMsg : Msg -> msg
    , onUpdate : ( Model, Cmd msg ) -> ( model, Cmd msg )
    , onReady : ( Model, Cmd msg ) -> ( model, Cmd msg )
    , onSelectChat : String -> ( Model, Cmd msg ) -> ( model, Cmd msg )
    , onSelectAgentChat : String -> ( Model, Cmd msg ) -> ( model, Cmd msg )
    , onGoToAgents : ( Model, Cmd msg ) -> ( model, Cmd msg )
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

        SelectAgentChat agentId ->
            ( model, Cmd.none )
                |> protocol.onSelectAgentChat agentId

        GoToAgents ->
            ( model, Cmd.none )
                |> protocol.onGoToAgents

        FetchChats ->
            ( { model | loadingChats = True, error = Nothing }
            , fetchChats protocol.toMsg
            )
                |> protocol.onUpdate

        FetchAgents ->
            ( { model | loadingAgents = True, error = Nothing }
            , fetchAgents protocol.toMsg
            )
                |> protocol.onUpdate

        GotChats result ->
            case result of
                Ok chats ->
                    ( { model | chats = chats, loadingChats = False }
                    , Cmd.none
                    )
                        |> protocol.onUpdate

                Err _ ->
                    ( { model | error = Just "Failed to load chats", loadingChats = False }
                    , Cmd.none
                    )
                        |> protocol.onUpdate

        GotAgents result ->
            case result of
                Ok agents ->
                    ( { model | agents = agents, loadingAgents = False }
                    , Cmd.none
                    )
                        |> protocol.onUpdate

                Err _ ->
                    ( { model | error = Just "Failed to load agents", loadingAgents = False }
                    , Cmd.none
                    )
                        |> protocol.onUpdate

        RequestDeleteChat chatId ->
            ( { model | deletingChatId = Just chatId }
            , Cmd.none
            )
                |> protocol.onUpdate

        ConfirmDeleteChat ->
            case model.deletingChatId of
                Just chatId ->
                    ( { model | deletingChatId = Nothing }
                    , deleteChat protocol.toMsg chatId
                    )
                        |> protocol.onUpdate

                Nothing ->
                    ( model, Cmd.none )
                        |> protocol.onUpdate

        CancelDeleteChat ->
            ( { model | deletingChatId = Nothing }
            , Cmd.none
            )
                |> protocol.onUpdate

        ChatDeleted result ->
            case result of
                Ok _ ->
                    -- Refresh the chat list after deletion
                    ( { model | loadingChats = True }
                    , fetchChats protocol.toMsg
                    )
                        |> protocol.onUpdate

                Err _ ->
                    ( { model | error = Just "Failed to delete chat" }
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


{-| Fetch the agent list from the server.
-}
fetchAgents : (Msg -> msg) -> Cmd msg
fetchAgents toMsg =
    Http.get
        { url = "/api/agents"
        , expect = Http.expectJson (toMsg << GotAgents) agentsDecoder
        }


{-| Delete a chat by ID.
-}
deleteChat : (Msg -> msg) -> String -> Cmd msg
deleteChat toMsg chatId =
    Http.request
        { method = "DELETE"
        , headers = []
        , url = "/api/chats/" ++ chatId
        , body = Http.emptyBody
        , expect = Http.expectWhatever (toMsg << ChatDeleted)
        , timeout = Nothing
        , tracker = Nothing
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


{-| Decoder for a list of agent info.
-}
agentsDecoder : Decoder (List AgentInfo)
agentsDecoder =
    Decode.list agentInfoDecoder


{-| Decoder for a single agent info.
-}
agentInfoDecoder : Decoder AgentInfo
agentInfoDecoder =
    Decode.map4 AgentInfo
        (Decode.field "id" Decode.string)
        (Decode.field "name" Decode.string)
        (Decode.field "instructions" Decode.string)
        (Decode.field "created_at" Decode.string)
