package cli_test

import (
	"os"
	"os/exec"
	"testing"

	"github.com/stretchr/testify/require"

	"a.yandex-team.ru/library/go/core/log"
	"a.yandex-team.ru/yt/chyt/controller/internal/tests/helpers"
	"a.yandex-team.ru/yt/go/guid"
	"a.yandex-team.ru/yt/go/yttest"
)

type cliRunner struct {
	EnvVariables map[string]string

	env *yttest.Env
	t   *testing.T
}

func NewCLIRunner(env *yttest.Env, t *testing.T) cliRunner {
	return cliRunner{
		EnvVariables: map[string]string{
			"YT_TEST_USER": "root",
		},
		env: env,
		t:   t,
	}
}

func (r *cliRunner) RunYT(args ...string) error {
	r.env.L.Debug("running command", log.Strings("args", args))
	require.GreaterOrEqual(r.t, len(args), 1)

	ytPath := os.Getenv("ARCADIA_BUILD_ROOT") + "/yt/python/yt/wrapper/bin/yt_make/yt"
	cmd := exec.Command(ytPath, args...)
	cmd.Env = os.Environ()
	for key, value := range r.EnvVariables {
		cmd.Env = append(cmd.Env, key+"="+value)
	}

	output, err := cmd.CombinedOutput()
	r.env.L.Debug("command finished", log.ByteString("output", output))
	return err
}

func TestCLISimple(t *testing.T) {
	t.Parallel()

	env, c := helpers.PrepareAPI(t)
	r := NewCLIRunner(env, t)

	r.EnvVariables["CHYT_CTL_ADDRESS"] = c.Endpoint

	err := r.RunYT("clickhouse", "ctl", "-h")
	require.NoError(t, err)

	alias := guid.New().String()
	err = r.RunYT("clickhouse", "ctl", "create", alias)
	require.NoError(t, err)

	ok, err := env.YT.NodeExists(env.Ctx, helpers.StrawberryRoot.Child(alias), nil)
	require.NoError(t, err)
	require.True(t, ok)
}

func TestCLIControllerUnavailable(t *testing.T) {
	t.Parallel()

	env := helpers.PrepareEnv(t)
	r := NewCLIRunner(env, t)

	r.EnvVariables["CHYT_CTL_ADDRESS"] = "http://this.address.does.not.exists"

	// Should fail since ctl service is unavailable.
	err := r.RunYT("clickhouse", "ctl", "-h")
	require.Error(t, err)

	// Other commands should not depend on controller address.
	err = r.RunYT("list", "/")
	require.NoError(t, err)
}
