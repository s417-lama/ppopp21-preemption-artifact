data_dirs = [
    "~/playground/results/change_num_es_pthread/latest",
    # "~/playground/results/change_num_es_preemption_1000/latest",
    "~/playground/results/change_num_es_preemption_10000/latest",
    "~/playground/results/change_num_es_no_preemption/latest",
]

opts = [
    {"IOMP"                    , "#1f77b4", "solid"  , "circle"      },
    # {"BOLT (preemptive; 1 ms)" , "#d62728", "dashdashdot", "cross-thin"  },
    {"BOLT (preemptive; 10 ms)", "#ff7f0e", "dashdot", "square-open" },
    {"BOLT (non-preemptive)"   , "#2ca02c", "dot"    , "diamond-open"},
]

n_workers = :lists.seq(28, 55, 3)

linewidth = 2

get_data_in_file = fn path ->
    [_ | times] =
        File.stream!(path)
        |> Stream.map(&String.split/1)
        |> Stream.map(fn
            ["[elapsed]", time, _] -> String.to_integer(time) / 1_000_000_000
            _ -> nil
        end)
        |> Stream.filter(&!is_nil(&1))
        |> Enum.to_list()
    times
end

get_data_in_dir = fn path ->
    Enum.map(n_workers, fn w ->
        times =
            "#{path}/w_#{w}_*.out"
            |> Path.expand()
            |> Path.wildcard()
            |> Enum.map(get_data_in_file)
            |> Enum.map(fn ts -> Statistics.mean(ts) end)
        {w, times}
    end)
end

Enum.zip(data_dirs, opts)
|> Enum.map(fn {dir, opt} ->
    {title, color, dash, symbol} = opt
    {xs, times} =
        get_data_in_dir.(dir)
        |> Enum.sort()
        |> Enum.unzip()
    {ys, errors} =
        times
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
            dash: dash,
        },
        marker: %{
            symbol: symbol,
            size:   14,
            line:   %{width: linewidth, color: color},
        },
    }
end)
|> List.insert_at(-1, %{
    x: Enum.map(1..1000, fn i -> 28 + (i / 1000) * 28 end),
    y: Enum.map(1..1000, fn i -> 0.363 * 56 / (28 + (i / 1000) * 28) end),
    name: "ideal",
    mode: "lines",
    line: %{
        width: linewidth,
        color: "#333333",
        dash:  "dash",
    },
})
|> PlotlyEx.plot(%{
    width:  500,
    height: 400,
    xaxis: %{
        showgrid: true,
        title: %{text: "# of workers"},
        showline: true,
    },
    yaxis: %{
        title: %{text: "Elapsed time (s)"},
        showline: true,
        range: [0, 0.9],
    },
    legend: %{
        x: 0.05,
        y: 0,
        bgcolor: "rgba(0,0,0,0)",
    },
    font: %{
        size: 22,
    },
    margin: %{
        l: 75,
        r: 10,
        b: 70,
        t: 10,
        pad: 0,
    },
})
|> PlotlyEx.show()

