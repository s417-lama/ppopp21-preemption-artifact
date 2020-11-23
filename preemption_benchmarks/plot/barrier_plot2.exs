barrier_type = "pthread"
# barrier_type = "busywait"
# barrier_type = "busywait_yield"

# pthread_dir   = "~/playground/results/barrier_#{barrier_type}_pthread/latest"
pthread_dir   = "~/playground/results/barrier_knl_#{barrier_type}_pthread/latest"
pthread_color = "#2ca02c"
pthread_dash  = "dash"

n_threads_per_core    = 10
n_barriers_per_thread = 1000

linewidth = 2

data_dirs = [
    # "~/playground/results/barrier_#{barrier_type}_new_es/latest",
    # "~/playground/results/barrier_#{barrier_type}_yield/latest",

    "~/playground/results/barrier_knl_#{barrier_type}_new_es/latest",
    "~/playground/results/barrier_knl_#{barrier_type}_yield/latest",
]

opts = [
    {"KLT-switching", "#1f77b4", "solid"  ,"circle-open"},
    {"Signal-yield" , "#ff7f0e", "dashdot","square-open"},
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
# pthread_y           = Statistics.median(pthread_data)
# pthread_q1          = Statistics.quartile(pthread_data, :first)
# pthread_q3          = Statistics.quartile(pthread_data, :third)
pthread_y         = Statistics.mean(pthread_data)
pthread_error     = Statistics.stdev(pthread_data)

Enum.zip(data_dirs, opts)
|> Enum.map(fn {data_dir, opt} ->
    {title, color, dash, symbol} = opt
    {xs, times} =
        data_dir
        |> get_data_in_dir.()
        |> Enum.sort()
        |> Enum.unzip()
    {ys, errors} =
        times
        |> Enum.map(fn t -> {Statistics.mean(t), Statistics.stdev(t)} end)
        |> Enum.unzip()
    %{
        x: xs,
        y: ys,
        error_y: %{
            type:      "data",
            array:     errors,
            visible:   true,
            thickness: linewidth,
        },
        name: title,
        line: %{
            color: color,
            width: linewidth,
            dash:  dash,
        },
        marker: %{
            symbol: symbol,
            size:   12,
            line:   %{width: linewidth, color: color},
        },
    }
end)
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
        range: [1.95, 4.3],
    },
    yaxis: %{
        type:  "log",
        dtick: "D1",
        title: %{text: "Time / barrier (&#x03BC;s)"},
        tickangle: 0,
        showline: true,
        ticks: "outside",
        tickvals: [10,20,30,40,50,60,70,80,90,100,200,300,400,500,600,700,800,900,1000,2000,3000,4000,5000,6000,7000,8000,9000,10000,20000,30000,40000,50000,60000,70000,80000,90000,100000],
        ticktext: [10,"","","","","","","","",100,"","","","","","","","",1000,"","","","","","","","",10000,"","","","","","","","",100000],
        range: [1.6, 4.15],
    },
    legend: %{
        x: 0.5,
        y: 0.1,
    },
    font: %{
        size: 22,
    },
    margin: %{
        l: 100,
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
            # y0: pthread_q1,
            # y1: pthread_q3,
            y0: pthread_y - pthread_error,
            y1: pthread_y + pthread_error,
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
})
|> PlotlyEx.show()
