# shellcheck shell=bash
# Sourced by a workflow step that pushes: gives git the job's GH_TOKEN for this step's process only,
# the header actions/checkout would have persisted in .git/config. Masks the encoded copy too.
# Usage: GH_TOKEN=... ; source tools/ci-git-auth.sh
: "${GH_TOKEN:?GH_TOKEN is not set}"
_ci_git_auth=$(printf 'x-access-token:%s' "$GH_TOKEN" | base64 -w0)
echo "::add-mask::$_ci_git_auth"
export GIT_CONFIG_COUNT=1
export GIT_CONFIG_KEY_0=http.https://github.com/.extraheader
export GIT_CONFIG_VALUE_0="AUTHORIZATION: basic $_ci_git_auth"
unset _ci_git_auth
