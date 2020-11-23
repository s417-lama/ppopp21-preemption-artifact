# barrier_type = "pthread"
barrier_type = "busywait"
# barrier_type = "busywait_yield"

pthread_dir   = "~/playground/results/barrier_#{barrier_type}_pthread/latest"
pthread_color = "#2ca02c"
pthread_dash  = "dash"

n_threads_per_core    = 10
n_barriers_per_thread = 1000

linewidth = 2

data_dirs = [
    "~/playground/results/barrier_#{barrier_type}_new_es/latest",
    "~/playground/results/barrier_#{barrier_type}_yield/latest",
]

opts = [
    {"KLT-switching", "#1f77b4", "solid"  },
    {"Signal-yield" , "#ff7f0e", "dashdot"},
]

get_data_in_file = fn path ->
    intvl =
        File.stream!(path)
        |> Stream.map(&String.split/1)
        |> Stream.map(fn
            ["Preemption", "timer", "interval:", intvl, _] -> intvl
            _ -> nil
        end)
        |> Stream.filter(&!is_nil(&1))
        |> Enum.to_list()
        |> case do
            [] -> nil
            [intvl] -> String.to_integer(intvl)
        end
    [_ | times] =
        File.stream!(path)
        |> Stream.map(&String.split/1)
        |> Stream.map(fn
            [_, _, _, "[elapsed]", time, _] -> String.to_integer(time) / (n_threads_per_core - 1) / n_barriers_per_thread / 1_000 # us
            _ -> nil
        end)
        |> Stream.filter(&!is_nil(&1))
        |> Enum.to_list()
    {intvl, times}
end

get_data_in_dir = fn path ->
    "#{path}/*.out"
    |> Path.expand()
    |> Path.wildcard()
    |> Enum.map(get_data_in_file)
    |> Enum.group_by(fn {intvl, _} -> intvl end, fn {_, times} -> times end)
    |> Enum.map(fn {intvl, times} -> {intvl, List.flatten(times)} end)
end

[{_, pthread_data}] = get_data_in_dir.(pthread_dir)
pthread_y           = Statistics.median(pthread_data)
pthread_q1          = Statistics.quartile(pthread_data, :first)
pthread_q3          = Statistics.quartile(pthread_data, :third)
# pthread_y         = Statistics.mean(pthread_data)
# pthread_error     = Statistics.stdev(pthread_data)

Enum.zip(data_dirs, opts)
|> Enum.map(fn {data_dir, opt} ->
    {title, color, dash} = opt
    mid = get_data_in_dir.(data_dir) |> Enum.sort()
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
            line: %{
                color: color,
                width: linewidth,
                dash:  dash,
            },
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
# show pthread legend by adding empty trace
|> List.insert_at(0, %{
    x:    [nil],
    y:    [nil],
    name: "Pthreads",
    mode: "lines",
    line: %{
        color: pthread_color,
        dash:  pthread_dash,
    },
})
|> List.insert_at(0, %{
    xaxis: "x2",
    y:    pthread_data,
    name: "Pthreads",
    type: "box",
    line: %{
        color: pthread_color,
        dash:  pthread_dash,
    },
    showlegend: false,
})
# Ideal line
|> List.insert_at(0, %{
    x:    [10, 100000],
    y:    [10, 100000],
    name: "Ideal",
    mode: "lines",
    line: %{
        color: "#555555",
        dash:  "dot",
    },
})
|> PlotlyEx.plot(%{
    grid:   %{rows: 1, columns: 2},
    width:  450,
    height: 350,
    xaxis: %{
        type:  "log",
        showgrid: true,
        dtick: "D1",
        title: %{text: "Timer interval (&#x03BC;s)"},
        tickangle: 0,
        showline: true,
        ticks: "outside",
        tickmode: 'array',
        tickvals: [100,200,300,400,500,600,700,800,900,1000,2000,3000,4000,5000,6000,7000,8000,9000,10000],
        ticktext: [100,"","","","","","","","",1000,"","","","","","","","",10000],
        domain: [0, 0.75],
        range: [1.95, 4.3],
    },
    xaxis2: %{
        domain: [0.75, 1],
    },
    yaxis: %{
        type:  "log",
        dtick: "D1",
        title: %{text: "Elapsed time (&#x03BC;s)"},
        tickangle: 0,
        showline: true,
        ticks: "outside",
        tickvals: [0.001,0.002,0.003,0.004,0.005,0.006,0.007,0.008,0.009,0.01,0.02,0.03,0.04,0.05,0.06,0.07,0.08,0.09,0.1,0.2,0.3,0.4,0.5,0.6,0.7,0.8,0.9,1,2,3,4,5,6,7,8,9,10],
        ticktext: [0.001,"","","","","","","","",0.01,"","","","","","","","",0.1,"","","","","","","","",1,"","","","","","","","",10],
        # range: [-2.6, 0.2],
    },
    legend: %{
        x: 0.5,
        y: 0.23,
    },
    font: %{
        size: 22,
    },
    margin: %{
        l: 90,
        r: 10,
        b: 70,
        t: 0,
        pad: 0,
    },
    shapes: [
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
    ],
    # annotations: [
    #     %{
    #         xref: "paper",
    #         yref: "paper",
    #         x: 0.95,
    #         y: 1,
    #         showarrow: false,
    #         text: "Ideal",
    #     }
    # ],
})
|> PlotlyEx.show()
