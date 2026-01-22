module Settings exposing
    ( SubmitShortcut(..)
    , AppSettings
    , defaultSettings
    , submitShortcutToString
    , submitShortcutFromString
    , submitShortcutLabel
    , allSubmitShortcuts
    , fetchSettings
    , saveSettings
    , settingsDecoder
    )

{-| Application settings module.

Handles settings like submit shortcut mode for the query input.
-}

import Http
import Json.Decode as Decode exposing (Decoder)
import Json.Encode as Encode


{-| Submit shortcut mode for the query input.
-}
type SubmitShortcut
    = EnterOnce -- Single Enter submits
    | ShiftEnter -- Shift+Enter submits
    | EnterTwice -- Double Enter (quick succession) submits


{-| Application settings.
-}
type alias AppSettings =
    { submitShortcut : SubmitShortcut
    }


{-| Default settings.
-}
defaultSettings : AppSettings
defaultSettings =
    { submitShortcut = ShiftEnter
    }


{-| All submit shortcut options.
-}
allSubmitShortcuts : List SubmitShortcut
allSubmitShortcuts =
    [ EnterOnce
    , ShiftEnter
    , EnterTwice
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


{-| Decoder for settings from the server.
-}
settingsDecoder : Decoder AppSettings
settingsDecoder =
    Decode.map AppSettings
        (Decode.field "submit_shortcut" submitShortcutDecoder)


{-| Decoder for submit shortcut.
-}
submitShortcutDecoder : Decoder SubmitShortcut
submitShortcutDecoder =
    Decode.string
        |> Decode.map submitShortcutFromString


{-| Encode settings for sending to the server.
-}
encodeSettings : AppSettings -> Encode.Value
encodeSettings settings =
    Encode.object
        [ ( "submit_shortcut", Encode.string (submitShortcutToString settings.submitShortcut) )
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
