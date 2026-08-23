# Evidence log — LLM provider tool-calling guarantees (Issue #4)

Researcher pass for open question 4 of `docs/CORE_DOCUMENT.md` §7.
Gathered 2026-08-23. The decision drawn from this log lives in
[`docs/adr/0001-llm-provider-for-the-assistant.md`](../adr/0001-llm-provider-for-the-assistant.md).

This file is the raw material. It separates what was **observed** from what a vendor
**states**, and records what could not be established at all.

---

## 0. What I could and could not observe

| Evidence class | Status |
|---|---|
| **Published SDK artefacts** — type definitions inside the vendors' own released packages | **Observed.** Downloaded, unpacked, quoted verbatim below. |
| **Vendor documentation** | **Observed as documents.** Pages fetched 2026-08-23, quoted with URLs. A vendor's statement about its own behaviour is a claim, not a measurement. |
| **Live API behaviour** — does `strict: true` actually hold? | **NOT OBSERVED.** No credentials for any provider exist in this environment (`ANTHROPIC_API_KEY`, `OPENAI_API_KEY`, `GEMINI_API_KEY` unset; no `ant` CLI; no `~/.config/anthropic` profile). No request was sent to any provider. |

**Consequence, stated plainly:** every guarantee below is a vendor claim, corroborated by
that vendor's own SDK types. None of it is an end-to-end measurement. The ADR is built so
its decision does not depend on any of these claims being true — see the validator
requirement.

`just-scrape` (named in the dispatch) is installed but has no `SGAI_API_KEY`; `just-scrape
validate` drops to an interactive key prompt and cannot run non-interactively. Evidence was
gathered with `WebFetch`/`WebSearch` and by inspecting published packages instead. Flagged
rather than worked around silently.

---

## 1. Observed: published SDK type definitions

Reproducible method:

```bash
python -m pip download --no-deps anthropic openai google-genai mistralai -d .
# unzip each wheel, then read the tool/function parameter types
```

Versions actually retrieved: `anthropic-1.0.0`, `openai-3.3.1`, `google_genai-2.19.0`,
`mistralai-2.9.4`.

### 1.1 Anthropic — `anthropic/types/tool_param.py` (v1.0.0)

```python
class ToolParam(TypedDict, total=False):
    input_schema: Required[InputSchema]
    name: Required[str]
    ...
    strict: bool
    """When true, guarantees schema validation on tool names and inputs"""
```

Note the scope of the claim: **tool names *and* inputs.** The tool name is covered, not
only the argument object.

### 1.2 OpenAI — `openai/types/shared_params/function_definition.py` (v3.3.1)

```python
class FunctionDefinition(TypedDict, total=False):
    name: Required[str]
    description: str
    parameters: FunctionParameters
    strict: Optional[bool]
    """Whether to enable strict schema adherence when generating the function call.

    If set to true, the model will follow the exact schema defined in the
    `parameters` field. Only a subset of JSON Schema is supported when `strict` is
    `true`. ..."""
```

`openai/types/responses/function_tool.py` carries the same flag: `strict: Optional[bool]`
— *"Whether strict parameter validation is enforced for this function tool."*

### 1.3 Google — `google/genai/types.py` (v2.19.0)

`FunctionDeclaration` has fields `description`, `name`, `parameters`,
`parameters_json_schema`, `response`, `response_json_schema`, `behavior`.

**There is no `strict` field.** `grep -n "strict" types.py` returns only unrelated hits
(enum prose, computer-use action space, image/video person-generation flags).

The guarantee, such as it is, is set per request rather than per tool, via
`FunctionCallingConfigMode`:

```python
class FunctionCallingConfigMode(_common.CaseInSensitiveEnum):
  AUTO = 'AUTO'
  ANY = 'ANY'
  NONE = 'NONE'
  VALIDATED = 'VALIDATED'
  """Model is constrained to predict either function calls or natural language
  response. If "allowed_function_names" are set, the predicted function calls will be
  limited to any one of "allowed_function_names", else the predicted function calls
  will be any one of the provided "function_declarations"."""
```

**Observed discrepancy.** The SDK docstring for `VALIDATED` describes only *which function*
may be predicted. It says nothing about whether the **arguments** conform to the declared
schema. Google's documentation says something different — see §2.3. I could not resolve the
discrepancy without a live call. **UNKNOWN.**

`parameters_json_schema` documents `additionalProperties: false` and `required` in its own
example, so the schema vocabulary exists; it is the enforcement claim that is unclear.

### 1.4 Mistral — `mistralai/client/models/function.py` (v2.9.4)

```python
class Function(BaseModel):
    name: str
    parameters: Dict[str, Any]
    description: Optional[str] = None
    strict: Optional[bool] = None
```

The field exists and carries **no docstring at all** — no stated guarantee in the SDK. Not
investigated further; Mistral was never a serious candidate here.

---

## 2. Observed: vendor documentation (fetched 2026-08-23)

### 2.1 Anthropic — strict tool use

Source: <https://platform.claude.com/docs/en/agents-and-tools/tool-use/strict-tool-use>

> Setting `strict: true` on a tool definition guarantees Claude's tool inputs match your
> JSON Schema by constraining the model's token sampling to schema-valid outputs (a
> technique called grammar-constrained sampling).

> **Guarantees:**
> * Tool `input` strictly follows the `input_schema`
> * Tool `name` is always valid (from provided tools or server tools)

The mechanism is named: grammar-constrained sampling — invalid tokens are masked during
decoding. A decode-time constraint, not a post-hoc check.

Also from that page, and directly relevant to core document §8.4, "what leaves the machine":

> Strict tool use compiles tool `input_schema` definitions into grammars ... Tool schemas
> are temporarily cached for up to 24 hours since last use. Prompts and responses are not
> retained beyond the API response.

So under strict mode the **Function schemas** are cached server-side for up to 24 hours,
separately from message content. The Function file's *shape* leaves the machine and lingers;
Project content still leaves only via a Rework-class Function. Worth knowing before §8.4 is
restated to users.

From <https://platform.claude.com/docs/en/agents-and-tools/tool-use/define-tools>:

> **Guaranteed tool calls with strict tools** — Combine `tool_choice: {"type": "any"}` with
> strict tool use to guarantee both that one of your tools will be called AND that the tool
> inputs strictly follow your schema.

### 2.2 Anthropic — the JSON Schema subset (the important limitation)

Source: <https://platform.claude.com/docs/en/build-with-claude/structured-outputs>
("JSON Schema limitations")

Supported: all basic types; `enum` (scalars only); `const`; `anyOf`/`allOf` (with limits);
`$ref`/`$defs` (internal only); `default`; `required`; `additionalProperties: false`; string
`format` (`date-time`, `time`, `date`, `duration`, `email`, `hostname`, `uri`, `ipv4`,
`ipv6`, `uuid`); array `minItems` **only for the values 0 and 1**.

**Not supported:**

> * Recursive schemas
> * Complex types within enums
> * External `$ref`
> * **Numerical constraints (such as `minimum`, `maximum`, `multipleOf`)**
> * **String constraints (`minLength`, `maxLength`)**

Models listed as supporting structured outputs: `claude-fable-5`, `claude-mythos-5`,
`claude-mythos-preview`, `claude-opus-5`, `claude-opus-4-8`, `claude-opus-4-7`,
`claude-opus-4-6`, `claude-sonnet-5`, `claude-sonnet-4-6`, `claude-sonnet-4-5-20250929`,
`claude-opus-4-5-20251101`, `claude-haiku-4-5-20251001`.

**Why this matters more here than in most projects.** Nearly every argument a Function in
this Studio will take is a bounded integer: MIDI note 0–127, velocity 0–127, step index
within a Pattern, bar number, Track index, tempo. `minimum`/`maximum` are exactly the
keywords that cannot be enforced. Anthropic's own documentation shows the workaround in its
example — a bounded integer expressed as an enumeration:

```json
"passengers": {"type": "integer", "enum": [1,2,3,4,5,6,7,8,9,10]}
```

For 0–127 that is a 128-entry enum per parameter. **Hypothesis, untested:** a 128-value
integer enum compiles and behaves acceptably. Anthropic's page states no enum-count limit;
OpenAI's does (1,000 across all enums). Nobody should build on this without trying it — see
the ADR's verification task.

### 2.3 Google Gemini

Source: <https://ai.google.dev/gemini-api/docs/function-calling>

Four modes via `tool_choice` in `generation_config`:

> - `auto` (Default): "Model decides whether to call a function or respond directly."
> - `any`: "Model is constrained to always predict a function call."
> - `none`: "Model is prohibited from making function calls."
> - `validated`: "Model ensures function schema adherence."

The same page states *"Only a subset of the OpenAPI schema is supported"* and that in `any`
mode *"the API may reject very large or deeply nested schemas."*

The API reference (<https://ai.google.dev/api/caching>, `FunctionCallingConfig.Mode`) renders
`VALIDATED` as the model choosing between a function call and a natural-language response
with constrained decoding applied — but the entry was truncated in the fetched page and I
could not obtain the full sentence.

**Assessment, stated honestly.** Gemini has *something* in this space. What exactly, is not
pinned down by any source I could reach:

- there is **no per-tool `strict` flag** — the guarantee, if any, is a request-level mode;
- the **SDK docstring and the documentation disagree** on whether arguments are covered (§1.3);
- no page I fetched states an explicit conformance guarantee the way Anthropic and OpenAI
  both do.

Third-party reports of forced-function-calling defects exist
(<https://adek.io/blog/gemini-forced-function-calling-is-broken/>). **Not verified by me.**
Recorded as a pointer for whoever tests this, not as a finding.

### 2.4 OpenAI

Sources: <https://developers.openai.com/api/docs/guides/function-calling> and
<https://developers.openai.com/api/docs/guides/structured-outputs>

> Setting `strict: true` ensures that function calls reliably adhere to the function schema,
> instead of being best effort.

Requirements: `additionalProperties: false` on every object, and **every** field in
`properties` listed in `required` — optionality is expressed as a nullable type union
(`"type": ["string", "null"]`).

Supported keywords include, for numbers: `multipleOf`, `minimum`, `maximum`,
`exclusiveMinimum`, `exclusiveMaximum`; for strings: `pattern`, `format`; for arrays:
`minItems`, `maxItems`. Limits: nesting ≤ 10, ≤ 5,000 total properties, ≤ 1,000 enum values
across all enums, ≤ 120,000 characters of names/enums/consts.

Caveats stated: on the Responses API strict normalisation is attempted by default and
**silently falls back to non-strict if the schema is incompatible**; Chat Completions stays
non-strict by default; schemas are processed and cached on first use, so varying schemas
cost latency and are **ineligible for zero data retention**. The structured-outputs guide
also warns that *"Structured Outputs can still contain mistakes"* — schema conformance is
not semantic correctness.

---

## 3. The finding that reverses the naive reading

Core document Appendix B.4 records strict tool use as "a direct mechanical match" for §3.8.
That is correct about the mechanism and **incomplete about its reach.**

| Constraint a Function actually needs | Anthropic `strict` | OpenAI `strict` | Gemini |
|---|---|---|---|
| Argument object conforms to schema | Yes, grammar-constrained | Yes | Claimed, unspecified |
| Tool name is one of the offered tools | Yes, stated explicitly | Not stated separately | `allowed_function_names` |
| Reject unknown properties | Yes (`additionalProperties: false`) | Yes (mandatory) | Not established |
| **Integer within 0–127** | **No** — enum workaround only | **Yes** (`minimum`/`maximum`) | Not established |
| String length bounds | **No** | Partly (`pattern`) | Not established |
| Array length bounds | `minItems` 0/1 only | `minItems`/`maxItems` | Not established |
| Guarantee attaches to | the Function (`strict` per tool) | the Function (`strict` per tool) | **the request, not the Function** |

Two things follow, and they point in opposite directions:

1. **No provider's strict mode is sufficient on its own.** The provider with the strongest
   and most clearly stated guarantee — Anthropic — is the one that *cannot* express the
   numeric range checks this domain is made of. A Function taking `velocity` will receive a
   schema-valid integer that may be `9000`. §9.2, "never perform a broad mutation," is not
   satisfied by strict mode alone.
2. **The providers' subsets differ in incompatible directions**, not merely by degree.
   Supporting Anthropic *and* OpenAI does not give one Function file with a weaker floor; it
   forces a choice between intersecting the subsets (discarding OpenAI's numeric bounds and
   gaining nothing) or maintaining a different enforceable schema per provider for the same
   Function. The second collides with §3.10, where the Function file is one manifest rebuilt
   from the toggles, and with §8.4's Directive/Rework labelling, which is auditable only if
   there is one thing to audit.

---

## 4. Honest UNKNOWNs

1. **No live call was made to any provider.** Every guarantee above is a vendor claim.
2. **Gemini's `VALIDATED` semantics are unresolved** — SDK docstring and documentation
   disagree, and the full reference text was unobtainable from the pages I could reach.
3. **Whether a 128-value integer enum is practical** under Anthropic strict mode — compile
   time, token cost, effect on model accuracy — is untested. This is the highest-value thing
   to measure once a key exists.
4. **Latency and cost of grammar compilation** for a Function file rebuilt whenever the user
   flips a switchboard toggle (§3.10) is unmeasured. Rebuilding the manifest plausibly
   invalidates both the compiled-grammar cache and the prompt cache on every toggle.
5. **Local / OpenAI-compatible runtimes** (Ollama, llama.cpp GBNF) were not investigated.
   Core document §1.1a settles the question ahead of the evidence — the Assistant requires a
   key — so this is out of scope, not merely unfinished.
