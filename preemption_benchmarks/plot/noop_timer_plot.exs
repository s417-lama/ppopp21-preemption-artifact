# no_preemption_dir = "~/playground/results/noop_no_preemption/latest"
no_preemption_dir = "~/playground/results/noop_no_preemption/2020-04-22_03-37-31"

preemption_type = "yield"
# preemption_type = "new_es_suspention_futex"
# preemption_type = "disabled"

data_dirs = [
    "~/playground/results/noop_preemption_#{preemption_type}_timer_signal_ngroups_1/latest",
    # "~/playground/results/noop_preemption_#{preemption_type}_timer_signal_ngroups_2/latest",
    # "~/playground/results/noop_preemption_#{preemption_type}_timer_signal_ngroups_4/latest",
    # "~/playground/results/noop_preemption_#{preemption_type}_timer_signal_ngroups_8/latest",
    # "~/playground/results/noop_preemption_#{preemption_type}_timer_signal_ngroups_14/latest",
    # "~/playground/results/noop_preemption_#{preemption_type}_timer_signal_ngroups_28/latest",
    # "~/playground/results/noop_preemption_#{preemption_type}_timer_signal_ngroups_56/latest",

    # "~/playground/results/noop_preemption_yield_timer_signal_ngroups_1/2020-04-22_03-05-05",
    "~/playground/results/noop_preemption_yield_timer_signal_ngroups_2/2020-04-22_03-06-56",
    "~/playground/results/noop_preemption_yield_timer_signal_ngroups_4/2020-04-22_03-07-25",
    "~/playground/results/noop_preemption_yield_timer_signal_ngroups_8/2020-04-22_03-09-58",
    "~/playground/results/noop_preemption_yield_timer_signal_ngroups_14/2020-04-22_03-10-38",
    "~/playground/results/noop_preemption_yield_timer_signal_ngroups_28/2020-04-22_03-11-30",
    "~/playground/results/noop_preemption_yield_timer_signal_ngroups_56/2020-04-22_03-11-59",

    # "~/playground/results/noop_preemption_disabled_timer_signal_ngroups_1/2020-04-22_03-13-11",
    # "~/playground/results/noop_preemption_disabled_timer_signal_ngroups_2/2020-04-22_03-13-50",
    # "~/playground/results/noop_preemption_disabled_timer_signal_ngroups_4/2020-04-22_03-14-22",
    # "~/playground/results/noop_preemption_disabled_timer_signal_ngroups_8/2020-04-22_03-15-33",
    # "~/playground/results/noop_preemption_disabled_timer_signal_ngroups_14/2020-04-22_03-32-36",
    # "~/playground/results/noop_preemption_disabled_timer_signal_ngroups_28/2020-04-22_03-34-38",
    # "~/playground/results/noop_preemption_disabled_timer_signal_ngroups_56/2020-04-22_03-35-49",

    # "~/playground/results/noop_preemption_#{preemption_type}_timer_signal_ngroups_56/latest",
    # "~/playground/results/noop_preemption_#{preemption_type}_ngroups_56/latest",
    # "~/playground/results/noop_preemption_#{preemption_type}_timer_dedicated_ngroups_1/latest",
]

opts = [
    {"# of groups = 1<br>(per-process)", "#e9a9ea", "solid", "circle"},
    # {"# of groups = 1"                 , "#e9a9ea", "solid", "circle"},
    {"# of groups = 2"                 , "#c58ed7", "solid", "circle"},
    {"# of groups = 4"                 , "#a274c1", "solid", "circle"},
    {"# of groups = 8"                 , "#7f5baa", "solid", "circle"},
    {"# of groups = 14"                , "#5c4394", "solid", "circle"},
    {"# of groups = 28"                , "#382d7e", "solid", "circle"},
    {"# of groups = 56<br>(per-worker)", "#001969", "solid", "circle"},
    # {"# of groups = 56"                , "#001969", "solid", "circle"},

    # {"Per-worker Signal-based", "#1f77b4", "solid"  , "circle-open"},
    # {"Per-process Dedicated"  , "#ff7f0e", "dashdot", "square-open"},
]

linewidth = 2

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
            [_, _, _, "[elapsed]", time, _] -> String.to_integer(time) / 1_000_000_000
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

[{_, no_preemption_times}] = get_data_in_dir.(no_preemption_dir)
no_preemption_y = Statistics.mean(no_preemption_times)

Enum.zip(data_dirs, opts)
|> Enum.map(fn {dir, opt} ->
    {title, color, dash, symbol} = opt
    {xs, times} =
        get_data_in_dir.(dir)
        |> Enum.sort()
        |> Enum.unzip()
    {ys, errors} =
        times
        |> Enum.map(fn t -> Enum.map(t, fn x -> x / no_preemption_y - 1 end) end)
        |> Enum.map(fn t -> {Statistics.mean(t), Statistics.stdev(t)} end)
        |> Enum.unzip()
    %{
        x: xs,
        y: ys,
        name: title,
        error_y: %{
            type:      "data",
            array:     errors,
            visible:   true,
            thickness: linewidth,
        },
        line: %{
            width: linewidth,
            color: color,
            dash: dash
        },
        marker: %{
            size: 12,
            # line: %{width: linewidth},
            symbol: symbol,
        },
    }
end)
|> PlotlyEx.plot(%{
    width:  500,
    height: 400,
    xaxis: %{
        type:  "log",
        showgrid: true,
        dtick: "D1",
        title: %{text: "Timer interval (&#x03BC;s)"},
        tickangle: 0,
        showline: true,
        ticks: "outside",
        tickvals: [100,200,300,400,500,600,700,800,900,1000,2000,3000,4000,5000,6000,7000,8000,9000,10000],
        ticktext: [100,"","","","","","","","",1000,"","","","","","","","",10000],
        range:    [2, 4.1],
    },
    yaxis: %{
        title: %{text: "Overhead"},
        tickformat: ",.1%",
        showline: true,
        range: [0, 0.033],
        # range: [0, 0.1],
    },
    legend: %{
        # x: 0.25,
        x: 0.45,
        y: 1,
        bgcolor: "rgba(0,0,0,0)",
    },
    font: %{
        size: 22,
    },
    margin: %{
        l: 90,
        r: 15,
        b: 70,
        t: 10,
        pad: 0,
    },
    showlegend: false,
})
|> PlotlyEx.show()
