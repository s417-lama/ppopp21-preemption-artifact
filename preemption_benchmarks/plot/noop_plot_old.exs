pthread_file  = "~/playground/results/noop_pthread/latest/*_1.out"
pthread_color = "#9467bd"
pthread_dash  = "longdashdot"

no_preemption_file  = "~/playground/results/noop_no_preemption/latest/*_1.out"
no_preemption_color = "#8c564b"
no_preemption_dash  = "15px,3px,3px,3px,3px,3px"

files = [
    "~/playground/results/noop_preemption_yield/latest/*_1.out",
    "~/playground/results/noop_preemption_new_es/latest/*_1.out",
    "~/playground/results/noop_preemption_new_es_suspention/latest/*_1.out",
    "~/playground/results/noop_preemption_new_es_suspention_futex/latest/*_1.out",
    # "~/playground/results/noop_preemption_disabled/latest/*_1.out",

    # "~/playground/results/noop_preemption_yield/latest/*_g_1.out",
    # "~/playground/results/noop_preemption_yield/latest/*_g_2.out",
    # "~/playground/results/noop_preemption_yield/latest/*_g_4.out",
    # "~/playground/results/noop_preemption_yield/latest/*_g_8.out",
    # "~/playground/results/noop_preemption_yield/latest/*_g_14.out",
    # "~/playground/results/noop_preemption_yield/latest/*_g_28.out",
    # "~/playground/results/noop_preemption_yield/latest/*_g_56.out",

    # "~/playground/results/noop_preemption_disabled/latest/*_g_1.out",
    # "~/playground/results/noop_preemption_disabled/latest/*_g_2.out",
    # "~/playground/results/noop_preemption_disabled/latest/*_g_4.out",
    # "~/playground/results/noop_preemption_disabled/latest/*_g_8.out",
    # "~/playground/results/noop_preemption_disabled/latest/*_g_14.out",
    # "~/playground/results/noop_preemption_disabled/latest/*_g_28.out",
    # "~/playground/results/noop_preemption_disabled/latest/*_g_56.out",
]

opts = [
    {"yield"                     , "#1f77b4", "solid"  },
    {"new ES"                    , "#ff7f0e", "dot"    },
    {"new ES (suspention)"       , "#2ca02c", "dashdot"},
    {"new ES (suspention, futex)", "#d62728", "dash"   },
    # {"disabled"                  , "#8c564b", "dash"   },

    # {"yield (groups = 1)" , "#5cd25c"},
    # {"yield (groups = 2)" , "#34bf34"},
    # {"yield (groups = 4)" , "#2ca02c"},
    # {"yield (groups = 8)" , "#248124"},
    # {"yield (groups = 14)", "#1b621b"},
    # {"yield (groups = 28)", "#134413"},
    # {"yield (groups = 56)", "#0a250a"},

    # {"disabled (groups = 1)" , "#5cd25c"},
    # {"disabled (groups = 2)" , "#34bf34"},
    # {"disabled (groups = 4)" , "#2ca02c"},
    # {"disabled (groups = 8)" , "#248124"},
    # {"disabled (groups = 14)", "#1b621b"},
    # {"disabled (groups = 28)", "#134413"},
    # {"disabled (groups = 56)", "#0a250a"},
]

get_data_in_file = fn path ->
    intvl =
        File.stream!(path)
        |> Stream.filter(fn line -> String.contains?(line, "Preemption timer interval:") end)
        |> Stream.map(&String.split/1)
        |> Stream.map(fn list -> Enum.at(list, 3) |> String.to_integer() end)
        |> Enum.to_list()
        |> case do
            []  -> nil
            [t] -> t
        end
    # ignore the first data
    [_ | data] =
    # data =
        File.stream!(path)
        |> Stream.filter(fn line -> String.contains?(line, "[elapsed]") end)
        |> Stream.map(&String.split/1)
        |> Stream.map(fn list -> (Enum.at(list, 4) |> String.to_integer()) / 1_000_000_000 end)
        |> Enum.to_list()
    {intvl, data}
end

[pthread_path]    = Path.expand(pthread_file) |> Path.wildcard()
{_, pthread_data} = get_data_in_file.(pthread_path)
pthread_y         = Statistics.median(pthread_data)
pthread_q1        = Statistics.quartile(pthread_data, :first)
pthread_q3        = Statistics.quartile(pthread_data, :third)

[no_preemption_path]    = Path.expand(no_preemption_file) |> Path.wildcard()
{_, no_preemption_data} = get_data_in_file.(no_preemption_path)
no_preemption_y         = Statistics.median(no_preemption_data)
no_preemption_q1        = Statistics.quartile(no_preemption_data, :first)
no_preemption_q3        = Statistics.quartile(no_preemption_data, :third)

Enum.zip(files, opts)
|> Enum.map(fn {file, opt} ->
    {title, color, dash} = opt
    mid =
        file
        |> Path.expand()
        |> Path.wildcard()
        |> Enum.map(get_data_in_file)
        |> Enum.sort()
    {m_xs, m_ys} = Enum.unzip(mid)
    {xs, ys} =
        mid
        |> Enum.map(fn {x, data} ->
            Enum.map(data, fn d -> {x, d} end)
        end)
        |> List.flatten()
        |> Enum.unzip()
    [
        %{
            x:    m_xs,
            y:    Enum.map(m_ys, &Statistics.median/1),
            mode: "lines",
            # y:    Enum.map(m_ys, &Statistics.mean/1),
            # marker: %{symbol: "cross"},
            name: title,
            line: %{color: color, dash: dash},
        },
        %{
            x:    xs,
            y:    ys,
            name: title,
            type: "box",
            marker: %{size: 5, color: color},
            boxmean: true,
            showlegend: false,
        },
    ]
end)
|> List.flatten()
# show no preemption legend by adding empty trace
|> List.insert_at(-1, %{
    x:    [nil],
    y:    [nil],
    name: "no preemption",
    mode: "lines",
    line: %{
        color: no_preemption_color,
        dash:  no_preemption_dash,
    },
})
|> List.insert_at(-1, %{
    xaxis: "x2",
    y:    no_preemption_data,
    name: "no preemption",
    type: "box",
    line: %{
        color: no_preemption_color,
        dash:  no_preemption_dash,
    },
    showlegend: false,
})
# show pthread legend by adding empty trace
|> List.insert_at(-1, %{
    x:    [nil],
    y:    [nil],
    name: "pthread",
    mode: "lines",
    line: %{
        color: pthread_color,
        dash:  pthread_dash,
    },
})
|> List.insert_at(-1, %{
    xaxis: "x2",
    y:    pthread_data,
    name: "pthread",
    type: "box",
    line: %{
        color: pthread_color,
        dash:  pthread_dash,
    },
    showlegend: false,
})
|> PlotlyEx.plot(%{
    grid:   %{rows: 1, columns: 2},
    width:  700,
    height: 500,
    xaxis: %{
        type:  "log",
        showgrid: true,
        dtick: "D1",
        title: %{text: "Timer interval (usec)"},
        tickangle: 0,
        showline: true,
        ticks: "outside",
        tickmode: 'array',
        tickvals: [100,200,300,400,500,600,700,800,900,1000,2000,3000,4000,5000,6000,7000,8000,9000,10000],
        ticktext: [100,"","","","","","","","",1000,"","","","","","","","",10000],
        range:    [1.95, 4.1],
        domain: [0, 0.85],
    },
    xaxis2: %{
        tickangle: 40,
        showgrid: true,
        domain: [0.85, 1],
    },
    yaxis: %{
        # type:  "log",
        # dtick: "D1",
        title: %{text: "Elapsed time (sec)"},
        showline: true,
        range: [0, 0.255],
    },
    legend: %{
        x: 0.65,
        y: 1,
    },
    font: %{
        size: 14,
    },
    margin: %{
        l: 60,
        r: 20,
        b: 75,
        t: 0,
        pad: 0,
    },
    shapes: [
        # no preemption
        %{
            type: "rect",
            xref: "paper",
            x0: 0,
            x1: 1,
            y0: no_preemption_q1,
            y1: no_preemption_q3,
            fillcolor: no_preemption_color,
            line: %{
                width: 0,
            },
            opacity: 0.2,
        },
        %{
            type: "line",
            xref: "paper",
            x0: 0,
            x1: 1,
            y0: no_preemption_y,
            y1: no_preemption_y,
            line: %{
                color: no_preemption_color,
                dash:  no_preemption_dash,
            },
        },
        # pthread
        %{
            type: "rect",
            xref: "paper",
            x0: 0,
            x1: 1,
            y0: pthread_q1,
            y1: pthread_q3,
            fillcolor: pthread_color,
            line: %{
                width: 0,
            },
            opacity: 0.2,
        },
        %{
            type: "line",
            xref: "paper",
            x0: 0,
            x1: 1,
            y0: pthread_y,
            y1: pthread_y,
            line: %{
                color: pthread_color,
                dash:  pthread_dash,
            },
        },
    ]
})
|> PlotlyEx.show()
