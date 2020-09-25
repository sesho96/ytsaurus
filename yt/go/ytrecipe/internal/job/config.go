package job

import (
	"time"

	"a.yandex-team.ru/yt/go/ypath"
)

const (
	DefaultBaseLayer     = ypath.Path("//porto_layers/ubuntu-xenial-base.tar.xz")
	MemoryReserve        = 128 * (1 << 20)
	OperationTimeReserve = time.Minute * 5
)

type OperationConfig struct {
	Cluster string `json:"cluster"`
	Pool    string `json:"pool"`

	// Title операции. Должен быть заполнен строкой, по которой будет удобно искать операцию в архиве.
	//
	// Например: [TS] yt/yt/tests/integration/ytrecipe [test_layers.py] [0/3]
	Title string `json:"title"`

	// CypressRoot задаёт директорию для хранения всех данных.
	CypressRoot ypath.Path    `json:"cypress_root"`
	OutputTTL   time.Duration `json:"output_ttl"`
	BlobTTL     time.Duration `json:"blob_ttl"`

	// CoordinateUpload включает оптимизированную загрузку артефактов.
	//
	// Обычная загрузка не координирует загрузку одного и того же файла между несколькими процессами.
	//
	// Если запустить одновременно 100 процессов ytexec с новым файлом, которого еще нет в кеше,
	// то все 100 пойдут загружать один и тот же файл в кипарис. В случае координации, процессы договорятся между
	// собой используя дин-таблицу. Один процесс займётся загрузкой, а 99 будут ждать результата.
	CoordinateUpload bool `json:"coordinate_upload"`

	// CPULimit будет поставлен в спеку операции. При этом, CPU reclaim
	// для этой операции будет выключен. Это значит, что у джоба не будут забирать CPU,
	// если он потребляет меньше лимита.
	CPULimit float64 `json:"cpu_limit"`
	// MemoryLimit будет поставлен в спеку операции. При этом,
	// `memory_reserve_factor` для этой операции будет выставлен в 1.0. Это значит,
	// что шедулер будет сразу запускать операцию с нужной гарантией, и будет выключен
	// алгоритм подгонки `memory_limit`. Внутри джобы, будет создан вложенный контейнер,
	// на котором будет включен memory контроллер. Потребление памяти и tmpfs внутри
	// этого контейнера будет засчитываться в общий лимит. В случае OOM, команда
	// будет убита, но джоб, операция и ytexec завершатся успешно. Информация о том,
	// что произошёл OOM будет доступна в ResultFile.
	MemoryLimit int `json:"memory_limit"`

	// Timeout задаёт общий таймаут на работу операции.
	Timeout time.Duration `json:"timeout"`

	EnablePorto   bool `json:"enable_porto"`
	EnableNetwork bool `json:"enable_network"`

	SpecPatch interface{} `json:"spec_patch"`
	TaskPatch interface{} `json:"task_patch"`
}

const (
	OutputDir = "output"
	CacheDir  = "cache"
	TmpDir    = "tmp"
)

func (c *OperationConfig) TmpDir() ypath.Path {
	return c.CypressRoot.Child(TmpDir)
}

func (c *OperationConfig) CacheDir() ypath.Path {
	return c.CypressRoot.Child(CacheDir)
}

func (c *OperationConfig) OutputDir() ypath.Path {
	return c.CypressRoot.Child(OutputDir)
}

type Cmd struct {
	// Args задаёт команду и её аргументы.
	Args []string `json:"args"`
	// Cwd задаёт рабочую директорию команды.
	Cwd string `json:"cwd"`
	// Environ задаёт переменные окружения, с которыми будет запущена команда.
	// Формат VAR_NAME=VAR_VALUE.
	Environ []string `json:"environ"`

	// *Timeout задают таймауты от старта выполнения команды, после которого команде будет послан сигнал.
	SIGUSR2Timeout time.Duration `json:"sigusr2_timeout"`
	SIGQUITTimeout time.Duration `json:"sigquit_timeout"`
	SIGKILLTimeout time.Duration `json:"sigkill_timeout"`
}
