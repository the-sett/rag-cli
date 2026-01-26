module Settings exposing
    ( SettingsTab(..)
    , SubmitShortcut(..)
    , ReasoningEffort(..)
    , Provider(..)
    , AppSettings
    , defaultSettings
    , submitShortcutToString
    , submitShortcutFromString
    , submitShortcutLabel
    , allSubmitShortcuts
    , reasoningEffortToString
    , reasoningEffortFromString
    , reasoningEffortLabel
    , allReasoningEfforts
    , providerToString
    , providerFromString
    , providerLabel
    , allProviders
    , fetchSettings
    , saveSettings
    , settingsDecoder
    , fetchModels
    )

{-| Application settings module.

Handles settings like submit shortcut mode for the query input,
AI provider and model selection, and reasoning effort level.
-}

import Http
import Json.Decode as Decode exposing (Decoder)
import Json.Encode as Encode


{-| Settings tab for the settings modal navigation.
-}
type SettingsTab
    = EditingPreferencesTab
    | ProviderTab
    | OpenAITab
    | GeminiTab


{-| AI provider type.
-}
type Provider
    = OpenAI
    | Gemini


{-| Submit shortcut mode for the query input.
-}
type SubmitShortcut
    = EnterOnce -- Single Enter submits
    | ShiftEnter -- Shift+Enter submits
    | EnterTwice -- Double Enter (quick succession) submits


{-| Reasoning effort level for the AI model.
-}
type ReasoningEffort
    = Low
    | Medium
    | High


{-| Application settings.
-}
type alias AppSettings =
    { submitShortcut : SubmitShortcut
    , provider : Provider
    , model : String
    , reasoningEffort : ReasoningEffort
    }


{-| Default settings.
-}
defaultSettings : AppSettings
defaultSettings =
    { submitShortcut = ShiftEnter
    , provider = OpenAI
    , model = ""
    , reasoningEffort = Medium
    }


{-| All submit shortcut options.
-}
allSubmitShortcuts : List SubmitShortcut
allSubmitShortcuts =
    [ EnterOnce
    , ShiftEnter
    , EnterTwice
    ]


{-| All reasoning effort options.
-}
allReasoningEfforts : List ReasoningEffort
allReasoningEfforts =
    [ Low
    , Medium
    , High
    ]


{-| All provider options.
-}
allProviders : List Provider
allProviders =
    [ OpenAI
    , Gemini
    ]


{-| Convert a submit shortcut to its JSON string representation.
-}
submitShortcutToString : SubmitShortcut -> String
submitShortcutToString shortcut =
    case shortcut of
        EnterOnce ->
            "enter_once"

        ShiftEnter ->
            "shift_enter"

        EnterTwice ->
            "enter_twice"


{-| Convert a JSON string to a submit shortcut.
-}
submitShortcutFromString : String -> SubmitShortcut
submitShortcutFromString str =
    case str of
        "shift_enter" ->
            ShiftEnter

        "enter_twice" ->
            EnterTwice

        _ ->
            EnterOnce


{-| Human-readable label for a submit shortcut.
-}
submitShortcutLabel : SubmitShortcut -> String
submitShortcutLabel shortcut =
    case shortcut of
        EnterOnce ->
            "Press Enter Once"

        ShiftEnter ->
            "Press Shift+Enter"

        EnterTwice ->
            "Press Enter Twice Quickly"


{-| Convert a reasoning effort to its JSON string representation.
-}
reasoningEffortToString : ReasoningEffort -> String
reasoningEffortToString effort =
    case effort of
        Low ->
            "low"

        Medium ->
            "medium"

        High ->
            "high"


{-| Convert a JSON string to a reasoning effort.
-}
reasoningEffortFromString : String -> ReasoningEffort
reasoningEffortFromString str =
    case str of
        "low" ->
            Low

        "high" ->
            High

        _ ->
            Medium


{-| Human-readable label for a reasoning effort.
-}
reasoningEffortLabel : ReasoningEffort -> String
reasoningEffortLabel effort =
    case effort of
        Low ->
            "Low"

        Medium ->
            "Medium"

        High ->
            "High"


{-| Convert a provider to its JSON string representation.
-}
providerToString : Provider -> String
providerToString provider =
    case provider of
        OpenAI ->
            "openai"

        Gemini ->
            "gemini"


{-| Convert a JSON string to a provider.
-}
providerFromString : String -> Provider
providerFromString str =
    case str of
        "gemini" ->
            Gemini

        _ ->
            OpenAI


{-| Human-readable label for a provider.
-}
providerLabel : Provider -> String
providerLabel provider =
    case provider of
        OpenAI ->
            "OpenAI"

        Gemini ->
            "Google Gemini"


{-| Decoder for settings from the server.
-}
settingsDecoder : Decoder AppSettings
settingsDecoder =
    Decode.map4 AppSettings
        (Decode.field "submit_shortcut" submitShortcutDecoder)
        (Decode.field "provider" providerDecoder)
        (Decode.field "model" Decode.string)
        (Decode.field "reasoning_effort" reasoningEffortDecoder)


{-| Decoder for submit shortcut.
-}
submitShortcutDecoder : Decoder SubmitShortcut
submitShortcutDecoder =
    Decode.string
        |> Decode.map submitShortcutFromString


{-| Decoder for reasoning effort.
-}
reasoningEffortDecoder : Decoder ReasoningEffort
reasoningEffortDecoder =
    Decode.string
        |> Decode.map reasoningEffortFromString


{-| Decoder for provider.
-}
providerDecoder : Decoder Provider
providerDecoder =
    Decode.string
        |> Decode.map providerFromString


{-| Encode settings for sending to the server.
-}
encodeSettings : AppSettings -> Encode.Value
encodeSettings settings =
    Encode.object
        [ ( "submit_shortcut", Encode.string (submitShortcutToString settings.submitShortcut) )
        , ( "provider", Encode.string (providerToString settings.provider) )
        , ( "model", Encode.string settings.model )
        , ( "reasoning_effort", Encode.string (reasoningEffortToString settings.reasoningEffort) )
        ]


{-| Fetch settings from the server.
-}
fetchSettings : (Result Http.Error AppSettings -> msg) -> Cmd msg
fetchSettings toMsg =
    Http.get
        { url = "/api/settings"
        , expect = Http.expectJson toMsg settingsDecoder
        }


{-| Save settings to the server.
-}
saveSettings : (Result Http.Error AppSettings -> msg) -> AppSettings -> Cmd msg
saveSettings toMsg settings =
    Http.request
        { method = "PUT"
        , headers = []
        , url = "/api/settings"
        , body = Http.jsonBody (encodeSettings settings)
        , expect = Http.expectJson toMsg settingsDecoder
        , timeout = Nothing
        , tracker = Nothing
        }


{-| Fetch available models from the server.
-}
fetchModels : (Result Http.Error (List String) -> msg) -> Cmd msg
fetchModels toMsg =
    Http.get
        { url = "/api/models"
        , expect = Http.expectJson toMsg modelsDecoder
        }


{-| Decoder for models list.
-}
modelsDecoder : Decoder (List String)
modelsDecoder =
    Decode.field "models" (Decode.list Decode.string)
